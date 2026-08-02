#include "DefenseGameServer.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQueue>
#include <QRandomGenerator>
#include <QStringList>
#include <QtMath>

#include <algorithm>
#include <cstring>
#include <functional>
#include <limits>
#include <list>
#include <queue>
#include <string>
#include <vector>

#include "../CMySql.h"
#include "RVO.h"

namespace {
constexpr int kTickIntervalMs = 50;
constexpr qint64 kCombatIntervalMs = 1000;
constexpr qint64 kBroadcastIntervalMs = 100;
constexpr qint64 kFirstWaveDelayMs = 45000;
constexpr qint64 kWaveIntervalMs = GAME_WAVE_INTERVAL_SECONDS * 1000LL;
constexpr qint64 kWaveAssaultDurationMs = GAME_WAVE_ASSAULT_SECONDS * 1000LL;
constexpr qint64 kStabilizationMs = 10LL * 60 * 1000;
constexpr qint64 kReinforcementMs = 12LL * 60 * 60 * 1000;
constexpr qint64 kPersistIntervalMs = 10000;
constexpr qint64 kPlayerRespawnDelayMs = GAME_PLAYER_RESPAWN_SECONDS * 1000LL;
constexpr qint64 kPlayerRespawnInvulnerabilityMs =
    GAME_PLAYER_RESPAWN_INVULNERABLE_SECONDS * 1000LL;
constexpr int kWallHp = GAME_WALL_MAX_HP;
constexpr int kTurretHp = GAME_TURRET_MAX_HP;
constexpr int kMaxActiveZombies = 384;
constexpr int kDormantZombieMinimumCoveragePercent = 75;
constexpr int kDormantZombieBaselineDensity = 3;
constexpr int kDormantZombieReplenishBatch = 320;
constexpr qint64 kDormantZombieReplenishMs = 5000;
constexpr qint64 kDormantZombieAdvanceMs = 2000;
constexpr qint64 kDormantZombieMaterializeMs = 500;
constexpr int kZombieZoneSpawnBufferCells = 5;
constexpr int kZombieSpatialBucketSize = 8;
constexpr int kZombieSpatialColumns =
    (GAME_MAP_WIDTH + kZombieSpatialBucketSize - 1) / kZombieSpatialBucketSize;
constexpr int kZombieSpatialRows =
    (GAME_MAP_HEIGHT + kZombieSpatialBucketSize - 1) / kZombieSpatialBucketSize;
constexpr int kFlowSectorSize = 10;
constexpr int kFlowSectorColumns =
    (GAME_MAP_WIDTH + kFlowSectorSize - 1) / kFlowSectorSize;
constexpr int kFlowSectorRows =
    (GAME_MAP_HEIGHT + kFlowSectorSize - 1) / kFlowSectorSize;
constexpr int kZombieAiBudgetPerTick = kMaxActiveZombies;
constexpr float kZombieCollisionRadius = 0.14f;
constexpr float kGiantZombieCollisionRadius = 0.27f;
constexpr qint64 kZombieBlockedAttackDelayMs = 800;
constexpr qint64 kOverlapFallbackIntervalMs = 500;
constexpr qint64 kCrowdAvoidanceRefreshMs = 100;
constexpr float kZombieNearSimulationDistance = 28.0f;
constexpr float kZombieMidSimulationDistance = 52.0f;
constexpr float kZombieMaterializationClearance = 5.0f;
constexpr float kFlowPlayerSourceCost = 0.0f;
constexpr float kFlowExtractorSourceCost = 100000.0f;
constexpr float kFlowOffensiveBuildingSourceCost = 200000.0f;
constexpr qint64 kFlowFieldRefreshMs = 1000;
constexpr bool kUnlimitedMaterials = false;
constexpr float kPlayerRadius = 0.28f;
constexpr float kZombieBaseAttackRange = 0.72f;
constexpr float kZombieEliteAttackRangeBonus = 0.08f;
constexpr float kZombieBaseTargetSearchRange = 18.0f;
constexpr float kZombieEliteTargetSearchRangeBonus = 3.0f;
constexpr qint64 kZombiePathRefreshMs = 1200;
constexpr qint64 kZombieStuckTimeoutMs = 1000;
constexpr int kZombiePathRebuildBudgetPerTick = 8;
constexpr float kRvoNeighborDistance = 2.4f;
constexpr std::size_t kRvoMaxNeighbors = 12;
constexpr float kRvoTimeHorizon = 0.9f;
constexpr float kRvoObstacleTimeHorizon = 0.5f;
constexpr float kZombieBreachBaseCost = 2.5f;
constexpr float kZombieBreachPathSavingsRatio = 0.72f;
constexpr float kZombieBreachPathMinimumSavings = 6.0f;
constexpr int kWorldSchemaVersion = 8;

float distanceBetween(float x1, float y1, float x2, float y2)
{
    return qSqrt((x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2));
}

float distanceToCell(float x, float y, const QPoint& cell)
{
    const float nearestX = qBound(static_cast<float>(cell.x()), x,
                                  static_cast<float>(cell.x() + 1));
    const float nearestY = qBound(static_cast<float>(cell.y()), y,
                                  static_cast<float>(cell.y() + 1));
    return distanceBetween(x, y, nearestX, nearestY);
}

QPointF normalizedVector(float x, float y)
{
    const float length = qSqrt(x * x + y * y);
    if (length <= 0.0001f) {
        return QPointF();
    }
    return QPointF(x / length, y / length);
}

void copyText(char* target, qsizetype capacity, const QString& text)
{
    if (!target || capacity <= 0) {
        return;
    }
    const QByteArray bytes = text.toUtf8();
    qstrncpy(target, bytes.constData(), capacity);
}

QByteArray compressGameSnapshot(const STRU_GAME_STATE_RS& snapshot)
{
    const QByteArray raw(reinterpret_cast<const char*>(&snapshot), sizeof(snapshot));
    // 状态每 100ms 发送一次，低压缩档比最小包体更有利于交互延迟。
    const QByteArray compressed = qCompress(raw, 1);
    QByteArray packet;
    packet.reserve(1 + compressed.size());
    packet.append(static_cast<char>(_default_protocol_game_state_compressed_send));
    packet.append(compressed);
    return packet;
}
}

DefenseGameServer::DefenseGameServer(QObject* parent)
    : QObject(parent)
{
    bool worldIdValid = false;
    const int configuredWorldId = qEnvironmentVariableIntValue(
        "DISKSERVER_GAME_WORLD_ID", &worldIdValid);
    if (worldIdValid && configuredWorldId > 0) {
        m_worldId = configuredWorldId;
    }
    m_tickTimer.setInterval(kTickIntervalMs);
    connect(&m_tickTimer, &QTimer::timeout, this, &DefenseGameServer::tick);
}

void DefenseGameServer::setDependencies(CMySql* database, INet* network)
{
    m_database = database;
    m_network = network;
}

void DefenseGameServer::setWorldId(int worldId)
{
    if (!m_tickTimer.isActive() && worldId > 0) {
        m_worldId = worldId;
    }
}

int DefenseGameServer::worldId() const
{
    return m_worldId;
}

bool DefenseGameServer::hasSessions() const
{
    return !m_sessions.isEmpty();
}

void DefenseGameServer::setMapTransitionHandler(MapTransitionHandler handler)
{
    m_mapTransitionHandler = std::move(handler);
}

void DefenseGameServer::setZombieSpawnDistanceCells(float distanceCells)
{
    m_zombieSpawnDistanceCells = qMax(0.0f, distanceCells);
}

bool DefenseGameServer::initialize()
{
    if (!m_database || !m_network) {
        return false;
    }

    initializeMap();
    if (!loadWorld()) {
        initializeMap();
        generateMapFeatures();
    }
    while (m_zombies.size() > kMaxActiveZombies) {
        auto zombie = m_zombies.begin();
        ZombieState suspended = std::move(zombie.value());
        m_zombies.erase(zombie);
        suspended.path.clear();
        suspended.plannedBreaches.clear();
        m_suspendedZombies.insert(suspended.id, std::move(suspended));
    }
    m_zombies.reserve(kMaxActiveZombies);
    m_suspendedZombies.reserve(kMaxActiveZombies);
    m_zombiePool.reserve(kMaxActiveZombies);
    const QVector<bool> initialSpawnMask = zombieSpawnAllowedMask();
    int allowedCellCount = 0;
    int coveredCellCount = 0;
    int dormantPopulation = 0;
    for (int index = 0; index < initialSpawnMask.size(); ++index) {
        if (!initialSpawnMask.at(index)) {
            continue;
        }
        ++allowedCellCount;
        if (index < m_dormantZombieDensity.size()
            && m_dormantZombieDensity.at(index) > 0) {
            ++coveredCellCount;
            dormantPopulation += m_dormantZombieDensity.at(index);
        }
    }
    if (m_dormantZombieDensity.size() != GAME_MAP_WIDTH * GAME_MAP_HEIGHT
        || coveredCellCount * 100
            < allowedCellCount * kDormantZombieMinimumCoveragePercent
        || dormantPopulation < allowedCellCount * 2
        || dormantPopulation > allowedCellCount * 4) {
        initializeDormantHordes();
    }
    m_zombieSpatialBuckets.resize(kZombieSpatialColumns * kZombieSpatialRows);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_nextWaveAtMs <= now) {
        m_nextWaveAtMs = now + kFirstWaveDelayMs;
    }
    m_lastSimulationAtMs = now;
    m_lastCombatAtMs = now;
    m_lastBroadcastAtMs = now;
    m_lastPersistAtMs = now;
    m_lastPopulationAtMs = now;
    m_lastDormantReplenishAtMs = now;
    m_lastDormantAdvanceAtMs = now;
    m_lastDormantMaterializeAtMs = now;
    rebuildFlowField(now);
    dematerializeDistantZombies(now);
    rebuildZombieSpatialIndex();
    validateStabilizingZones();
    promoteStableZones(now);
    m_tickTimer.start();
    return true;
}

void DefenseGameServer::shutdown()
{
    m_tickTimer.stop();
    saveWorld();
    m_sessions.clear();
    for (auto it = m_players.begin(); it != m_players.end(); ++it) {
        it->online = false;
    }
}

void DefenseGameServer::handleJoin(ConnectionId socket,
                                   qint64 authenticatedUserId,
                                   const QString& userName,
                                   const STRU_GAME_JOIN_RQ& request)
{
    if (socket == 0 || authenticatedUserId <= 0 || request.m_userId != authenticatedUserId) {
        return;
    }

    const PlayerState* existingPlayer = playerForUser(authenticatedUserId);
    if (!existingPlayer || !existingPlayer->online) {
        int onlinePlayers = 0;
        for (auto it = m_players.constBegin(); it != m_players.constEnd(); ++it) {
            onlinePlayers += it->online ? 1 : 0;
        }
        if (onlinePlayers >= GAME_MAX_PLAYERS) {
            sendEvent(authenticatedUserId, _game_event_warning, QStringLiteral("战场人数已满。"));
            return;
        }
    }
    if (!existingPlayer) {
        int respawnZoneId = 0;
        const QPointF spawn = preferredPlayerSpawn(authenticatedUserId, 0,
                                                    &respawnZoneId);
        PlayerState player;
        player.userId = authenticatedUserId;
        player.userName = userName.left(GAME_NAME_SIZE - 1);
        player.x = static_cast<float>(spawn.x());
        player.y = static_cast<float>(spawn.y());
        player.spawnX = player.x;
        player.spawnY = player.y;
        player.respawnZoneId = respawnZoneId;
        m_players.insert(authenticatedUserId, player);
    }

    PlayerState& player = m_players[authenticatedUserId];
    refreshPlayerRespawnLocation(player);
    bool relocatedFromBuilding = false;
    if (player.respawnAtMs <= 0 && isPositionBlocked(player.x, player.y, kPlayerRadius)) {
        const QPointF safePosition = findSafePositionNear(player.x, player.y);
        player.x = static_cast<float>(safePosition.x());
        player.y = static_cast<float>(safePosition.y());
        player.moveX = 0.0f;
        player.moveY = 0.0f;
        player.hasMoveTarget = false;
        relocatedFromBuilding = true;
    }
    player.online = true;
    if (!userName.trimmed().isEmpty()) {
        player.userName = userName.left(GAME_NAME_SIZE - 1);
    }
    player.color = QColor(request.m_colorR, request.m_colorG, request.m_colorB);
    if (!player.color.isValid()) {
        player.color = QColor(38, 166, 154);
    }
    for (auto it = m_zones.begin(); it != m_zones.end(); ++it) {
        if (it->ownerId == authenticatedUserId) {
            it->color = player.color;
        }
    }
    m_sessions.insert(socket, authenticatedUserId);
    const qint64 arrivalTime = QDateTime::currentMSecsSinceEpoch();
    m_lastFlowFieldAtMs = 0;
    rebuildFlowField(arrivalTime);
    materializeNearbyHordes(arrivalTime, true);
    rebuildZombieSpatialIndex();
    if (relocatedFromBuilding) {
        sendEvent(authenticatedUserId, _game_event_warning,
                  QStringLiteral("原位置已有防御建筑，角色已移动到附近空地。"));
    }
    sendEvent(authenticatedUserId, _game_event_info, QStringLiteral("已进入安全区防线。"));
    broadcastSnapshot();
}

void DefenseGameServer::handleAction(ConnectionId socket,
                                     qint64 authenticatedUserId,
                                     const STRU_GAME_ACTION_RQ& request)
{
    if (authenticatedUserId <= 0 || request.m_userId != authenticatedUserId
        || m_sessions.value(socket, 0) != authenticatedUserId) {
        return;
    }

    PlayerState* player = playerForUser(authenticatedUserId);
    if (!player || !player->online) {
        return;
    }

    if (request.m_action == _game_action_set_respawn_zone) {
        processSetRespawnZone(*player, qFloor(request.m_x), qFloor(request.m_y));
        broadcastSnapshot();
        return;
    }
    if (player->respawnAtMs > 0) {
        return;
    }

    switch (request.m_action) {
    case _game_action_move:
        processMove(*player, request.m_x, request.m_y);
        break;
    case _game_action_move_target:
        processMoveTarget(*player, request.m_x, request.m_y);
        break;
    case _game_action_attack:
        return;
    case _game_action_build:
        processBuild(*player, qFloor(request.m_x), qFloor(request.m_y), request.m_buildingType);
        break;
    case _game_action_demolish:
        processDemolish(*player, qFloor(request.m_x), qFloor(request.m_y));
        break;
    case _game_action_upgrade:
        processUpgrade(*player, qFloor(request.m_x), qFloor(request.m_y));
        break;
    case _game_action_repair:
        processRepair(*player, qFloor(request.m_x), qFloor(request.m_y));
        break;
    case _game_action_leave:
        handleDisconnected(socket);
        return;
    default:
        return;
    }
    broadcastSnapshot();
}

void DefenseGameServer::handleDisconnected(ConnectionId socket)
{
    const qint64 userId = m_sessions.take(socket);
    if (userId <= 0 || userStillPresent(userId)) {
        return;
    }
    if (PlayerState* player = playerForUser(userId)) {
        player->online = false;
        player->moveX = 0.0f;
        player->moveY = 0.0f;
        player->hasMoveTarget = false;
    }
    broadcastSnapshot();
}

bool DefenseGameServer::takePlayerForTransition(ConnectionId socket, qint64 userId,
                                                PlayerTransferState& transfer)
{
    if (m_sessions.value(socket, 0) != userId) {
        return false;
    }
    const auto player = m_players.find(userId);
    if (player == m_players.end() || !player->online) {
        return false;
    }
    transfer.userName = player->userName;
    transfer.sourceX = player->x;
    transfer.sourceY = player->y;
    transfer.hp = player->hp;
    transfer.maxHp = player->maxHp;
    transfer.coins = player->coins;
    transfer.stone = player->stone;
    transfer.aluminum = player->aluminum;
    transfer.iron = player->iron;
    transfer.coal = player->coal;
    transfer.oil = player->oil;
    transfer.population = player->population;
    transfer.level = player->level;
    transfer.kills = player->kills;
    transfer.experience = player->experience;
    transfer.color = player->color;
    transfer.lastAttackAtMs = player->lastAttackAtMs;
    transfer.respawnAtMs = player->respawnAtMs;
    transfer.invulnerableUntilMs = player->invulnerableUntilMs;

    for (auto session = m_sessions.begin(); session != m_sessions.end();) {
        if (session.value() == userId) {
            session = m_sessions.erase(session);
        } else {
            ++session;
        }
    }
    for (auto repair = m_repairs.begin(); repair != m_repairs.end();) {
        if (repair->ownerId == userId) {
            repair = m_repairs.erase(repair);
        } else {
            ++repair;
        }
    }
    m_players.erase(player);
    saveWorld();
    broadcastSnapshot();
    return true;
}

void DefenseGameServer::acceptPlayerTransition(ConnectionId socket, qint64 userId,
                                               const QString& userName,
                                               const PlayerTransferState& transfer,
                                               int directionX, int directionY)
{
    if (socket == 0 || userId <= 0) {
        return;
    }
    PlayerState player;
    player.userId = userId;
    player.userName = userName.trimmed().isEmpty() ? transfer.userName : userName;
    player.hp = transfer.hp;
    player.maxHp = transfer.maxHp;
    player.coins = transfer.coins;
    player.stone = transfer.stone;
    player.aluminum = transfer.aluminum;
    player.iron = transfer.iron;
    player.coal = transfer.coal;
    player.oil = transfer.oil;
    player.population = qMax(0, transfer.population);
    player.level = transfer.level;
    player.kills = transfer.kills;
    player.experience = transfer.experience;
    player.color = transfer.color;
    player.lastAttackAtMs = transfer.lastAttackAtMs;
    player.respawnAtMs = transfer.respawnAtMs;
    player.invulnerableUntilMs = transfer.invulnerableUntilMs;

    int previousRespawnZoneId = 0;
    if (const PlayerState* previous = playerForUser(userId)) {
        previousRespawnZoneId = previous->respawnZoneId;
    }

    const float edgeInset = kPlayerRadius + 0.08f;
    player.x = directionX < 0 ? GAME_MAP_WIDTH - edgeInset
        : directionX > 0 ? edgeInset
        : qBound(edgeInset, transfer.sourceX, GAME_MAP_WIDTH - edgeInset);
    player.y = directionY < 0 ? GAME_MAP_HEIGHT - edgeInset
        : directionY > 0 ? edgeInset
        : qBound(edgeInset, transfer.sourceY, GAME_MAP_HEIGHT - edgeInset);
    const QPointF safePosition = findSafePositionNear(player.x, player.y);
    player.x = static_cast<float>(safePosition.x());
    player.y = static_cast<float>(safePosition.y());
    int selectedRespawnZoneId = 0;
    const QPointF respawn = preferredPlayerSpawn(userId, previousRespawnZoneId,
                                                  &selectedRespawnZoneId);
    if (selectedRespawnZoneId > 0) {
        player.spawnX = static_cast<float>(respawn.x());
        player.spawnY = static_cast<float>(respawn.y());
        player.respawnZoneId = selectedRespawnZoneId;
    } else {
        player.spawnX = player.x;
        player.spawnY = player.y;
    }
    player.online = true;
    m_players.insert(userId, player);
    m_sessions.insert(socket, userId);
    const qint64 arrivalTime = QDateTime::currentMSecsSinceEpoch();
    m_lastFlowFieldAtMs = 0;
    rebuildFlowField(arrivalTime);
    materializeNearbyHordes(arrivalTime, true);
    rebuildZombieSpatialIndex();
    sendEvent(userId, _game_event_success, QStringLiteral("已进入相邻战场区域。"));
    broadcastSnapshot();
}

bool DefenseGameServer::persistNow()
{
    return saveWorld();
}

void DefenseGameServer::tick()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    processPlayerRespawns(now);
    validateStabilizingZones();
    promoteStableZones(now);

    replenishDormantHordes(now);
    if (now - m_lastFlowFieldAtMs >= kFlowFieldRefreshMs) {
        rebuildFlowField(now);
    }
    dematerializeDistantZombies(now);
    advanceDormantHordes(now);
    materializeNearbyHordes(now);

    const qint64 elapsedMs = qBound<qint64>(1LL, now - m_lastSimulationAtMs, 150LL);
    m_lastSimulationAtMs = now;
    const float deltaSeconds = static_cast<float>(elapsedMs) / 1000.0f;
    processPlayerMovement(deltaSeconds);
    processPlayerRegeneration(now);
    processRepairs(now);
    rebuildZombieSpatialIndex();
    processPlayerAutoAttacks(now);
    processZombies(now, deltaSeconds);
    if (now - m_lastOverlapCorrectionAtMs >= kOverlapFallbackIntervalMs) {
        m_lastOverlapCorrectionAtMs = now;
        resolveZombieOverlaps();
    }

    if (now - m_lastCombatAtMs >= kCombatIntervalMs) {
        m_lastCombatAtMs = now;
        rebuildZombieSpatialIndex();
        processTurrets(now);
        removeDeadZombies();
    }
    resetWaveIfNoLivingPlayers(now);
    if (m_nextWaveAtMs > 0 && now >= m_nextWaveAtMs) {
        spawnWave(now);
    }
    processRandomDisasters(now);

    if (now - m_lastExtractionAtMs >= GAME_EXTRACTION_INTERVAL_MS) {
        m_lastExtractionAtMs = now;
        processExtractors(now);
    }
    if (now - m_lastPopulationAtMs >= GAME_POPULATION_INTERVAL_MS) {
        m_lastPopulationAtMs = now;
        processPopulation(now);
    }

    if (now - m_lastBroadcastAtMs >= kBroadcastIntervalMs) {
        m_lastBroadcastAtMs = now;
        broadcastSnapshot();
    }

    if (now - m_lastPersistAtMs >= kPersistIntervalMs) {
        m_lastPersistAtMs = now;
        saveWorld();
    }
}

int DefenseGameServer::cellIndex(int x, int y) const
{
    return y * GAME_MAP_WIDTH + x;
}

bool DefenseGameServer::inBounds(int x, int y) const
{
    return x >= 0 && y >= 0 && x < GAME_MAP_WIDTH && y < GAME_MAP_HEIGHT;
}

int DefenseGameServer::spatialBucketIndex(float x, float y) const
{
    const int bucketX = qBound(0, qFloor(x / kZombieSpatialBucketSize),
                               kZombieSpatialColumns - 1);
    const int bucketY = qBound(0, qFloor(y / kZombieSpatialBucketSize),
                               kZombieSpatialRows - 1);
    return bucketY * kZombieSpatialColumns + bucketX;
}

void DefenseGameServer::rebuildZombieSpatialIndex()
{
    if (m_zombieSpatialBuckets.size() != kZombieSpatialColumns * kZombieSpatialRows) {
        m_zombieSpatialBuckets.resize(kZombieSpatialColumns * kZombieSpatialRows);
    }
    for (QVector<int>& bucket : m_zombieSpatialBuckets) {
        bucket.clear();
    }
    for (auto zombie = m_zombies.constBegin(); zombie != m_zombies.constEnd(); ++zombie) {
        if (zombie->hp > 0) {
            m_zombieSpatialBuckets[spatialBucketIndex(zombie->x, zombie->y)]
                .append(zombie->id);
        }
    }
}

QVector<int> DefenseGameServer::nearbyZombieIds(float x, float y, float radius) const
{
    QVector<int> result;
    if (m_zombieSpatialBuckets.isEmpty()) {
        return result;
    }
    const int minBucketX = qBound(0, qFloor((x - radius) / kZombieSpatialBucketSize),
                                  kZombieSpatialColumns - 1);
    const int maxBucketX = qBound(0, qFloor((x + radius) / kZombieSpatialBucketSize),
                                  kZombieSpatialColumns - 1);
    const int minBucketY = qBound(0, qFloor((y - radius) / kZombieSpatialBucketSize),
                                  kZombieSpatialRows - 1);
    const int maxBucketY = qBound(0, qFloor((y + radius) / kZombieSpatialBucketSize),
                                  kZombieSpatialRows - 1);
    for (int bucketY = minBucketY; bucketY <= maxBucketY; ++bucketY) {
        for (int bucketX = minBucketX; bucketX <= maxBucketX; ++bucketX) {
            result += m_zombieSpatialBuckets.at(
                bucketY * kZombieSpatialColumns + bucketX);
        }
    }
    return result;
}

bool DefenseGameServer::isCellOccupiedByUnit(int x, int y) const
{
    for (auto it = m_players.constBegin(); it != m_players.constEnd(); ++it) {
        if (it->online && it->respawnAtMs <= 0 && qFloor(it->x) == x && qFloor(it->y) == y) {
            return true;
        }
    }
    for (auto it = m_zombies.constBegin(); it != m_zombies.constEnd(); ++it) {
        if (qFloor(it->x) == x && qFloor(it->y) == y) {
            return true;
        }
    }
    return false;
}

bool DefenseGameServer::isCellWalkable(int x, int y) const
{
    if (!inBounds(x, y)) {
        return false;
    }
    const CellState& cell = m_cells.at(cellIndex(x, y));
    return cell.terrain == 0 && cell.buildingType == _game_building_none;
}

bool DefenseGameServer::isPositionBlocked(float x, float y, float radius) const
{
    if (x < radius || y < radius || x > GAME_MAP_WIDTH - radius || y > GAME_MAP_HEIGHT - radius) {
        return true;
    }

    const int minX = qMax(0, qFloor(x - radius));
    const int maxX = qMin(GAME_MAP_WIDTH - 1, qFloor(x + radius));
    const int minY = qMax(0, qFloor(y - radius));
    const int maxY = qMin(GAME_MAP_HEIGHT - 1, qFloor(y + radius));
    for (int cellY = minY; cellY <= maxY; ++cellY) {
        for (int cellX = minX; cellX <= maxX; ++cellX) {
            const CellState& cell = m_cells.at(cellIndex(cellX, cellY));
            const bool blocksPlayer = cell.terrain != 0
                || (cell.buildingType != _game_building_none
                    && cell.buildingType != _game_building_door);
            if (blocksPlayer) {
                const float nearestX = qBound(static_cast<float>(cellX), x, static_cast<float>(cellX + 1));
                const float nearestY = qBound(static_cast<float>(cellY), y, static_cast<float>(cellY + 1));
                if (distanceBetween(x, y, nearestX, nearestY) < radius) {
                    return true;
                }
            }
        }
    }
    return false;
}

QPointF DefenseGameServer::findSpawnPosition() const
{
    const QPoint center(GAME_MAP_WIDTH / 2, GAME_MAP_HEIGHT / 2);
    for (int radius = 0; radius < qMax(GAME_MAP_WIDTH, GAME_MAP_HEIGHT); ++radius) {
        for (int y = center.y() - radius; y <= center.y() + radius; ++y) {
            for (int x = center.x() - radius; x <= center.x() + radius; ++x) {
                if (inBounds(x, y) && isCellWalkable(x, y) && !isCellOccupiedByUnit(x, y)) {
                    return QPointF(x + 0.5, y + 0.5);
                }
            }
        }
    }
    return QPointF(1.5, 1.5);
}

QPointF DefenseGameServer::findSafePositionNear(float preferredX, float preferredY) const
{
    const int originX = qBound(0, qFloor(preferredX), GAME_MAP_WIDTH - 1);
    const int originY = qBound(0, qFloor(preferredY), GAME_MAP_HEIGHT - 1);
    const int maxRadius = qMax(GAME_MAP_WIDTH, GAME_MAP_HEIGHT);
    for (int radius = 0; radius < maxRadius; ++radius) {
        for (int y = originY - radius; y <= originY + radius; ++y) {
            for (int x = originX - radius; x <= originX + radius; ++x) {
                if (qMax(qAbs(x - originX), qAbs(y - originY)) != radius
                    || !inBounds(x, y) || !isCellWalkable(x, y)
                    || isCellOccupiedByUnit(x, y)) {
                    continue;
                }
                return QPointF(x + 0.5, y + 0.5);
            }
        }
    }
    return findSpawnPosition();
}

QPointF DefenseGameServer::findSpawnPositionInZone(const ZoneState& zone) const
{
    if (zone.cells.isEmpty()) {
        return findSpawnPosition();
    }
    QPointF center;
    for (int index : zone.cells) {
        center += QPointF(index % GAME_MAP_WIDTH + 0.5,
                          index / GAME_MAP_WIDTH + 0.5);
    }
    center /= zone.cells.size();

    QVector<int> candidates(zone.cells.begin(), zone.cells.end());
    std::sort(candidates.begin(), candidates.end(), [this, center](int left, int right) {
        const float leftDistance = distanceBetween(
            left % GAME_MAP_WIDTH + 0.5f, left / GAME_MAP_WIDTH + 0.5f,
            static_cast<float>(center.x()), static_cast<float>(center.y()));
        const float rightDistance = distanceBetween(
            right % GAME_MAP_WIDTH + 0.5f, right / GAME_MAP_WIDTH + 0.5f,
            static_cast<float>(center.x()), static_cast<float>(center.y()));
        return leftDistance < rightDistance;
    });
    for (int index : candidates) {
        const int x = index % GAME_MAP_WIDTH;
        const int y = index / GAME_MAP_WIDTH;
        if (isCellWalkable(x, y)) {
            return QPointF(x + 0.5, y + 0.5);
        }
    }
    return findSpawnPosition();
}

QPointF DefenseGameServer::preferredPlayerSpawn(qint64 userId, int preferredZoneId,
                                                int* selectedZoneId) const
{
    if (selectedZoneId) {
        *selectedZoneId = 0;
    }
    const ZoneState* selected = nullptr;
    if (const ZoneState* preferred = zoneById(preferredZoneId);
        preferred && preferred->ownerId == userId && !preferred->cells.isEmpty()) {
        selected = preferred;
    }
    if (!selected) {
        for (auto zone = m_zones.constBegin(); zone != m_zones.constEnd(); ++zone) {
            if (zone->ownerId != userId || zone->cells.isEmpty()) {
                continue;
            }
            if (!selected || zone->state > selected->state
                || (zone->state == selected->state
                    && zone->cells.size() > selected->cells.size())) {
                selected = &zone.value();
            }
        }
    }
    if (!selected) {
        return findSpawnPosition();
    }
    if (selectedZoneId) {
        *selectedZoneId = selected->id;
    }
    return findSpawnPositionInZone(*selected);
}

bool DefenseGameServer::userStillPresent(qint64 userId) const
{
    return m_sessions.values().contains(userId);
}

DefenseGameServer::PlayerState* DefenseGameServer::playerForUser(qint64 userId)
{
    auto it = m_players.find(userId);
    return it == m_players.end() ? nullptr : &it.value();
}

const DefenseGameServer::PlayerState* DefenseGameServer::playerForUser(qint64 userId) const
{
    auto it = m_players.constFind(userId);
    return it == m_players.constEnd() ? nullptr : &it.value();
}

DefenseGameServer::ZoneState* DefenseGameServer::zoneById(int zoneId)
{
    auto it = m_zones.find(zoneId);
    return it == m_zones.end() ? nullptr : &it.value();
}

const DefenseGameServer::ZoneState* DefenseGameServer::zoneById(int zoneId) const
{
    auto it = m_zones.constFind(zoneId);
    return it == m_zones.constEnd() ? nullptr : &it.value();
}

DefenseGameServer::ZoneState* DefenseGameServer::adjacentZoneForBuilding(int x, int y)
{
    static const QPoint directions[] = {QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)};
    for (const QPoint& direction : directions) {
        const int nx = x + direction.x();
        const int ny = y + direction.y();
        if (!inBounds(nx, ny)) {
            continue;
        }
        const int zoneId = m_cells.at(cellIndex(nx, ny)).zoneId;
        if (ZoneState* zone = zoneById(zoneId)) {
            return zone;
        }
    }
    return nullptr;
}

const DefenseGameServer::ZoneState* DefenseGameServer::protectingZoneForCell(int x, int y) const
{
    if (!inBounds(x, y)) {
        return nullptr;
    }
    const CellState& cell = m_cells.at(cellIndex(x, y));
    if (const ZoneState* zone = zoneById(cell.zoneId)) {
        return zone;
    }
    if (cell.buildingType == _game_building_none) {
        return nullptr;
    }
    static const QPoint directions[] = {
        QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)
    };
    for (const QPoint& direction : directions) {
        const int nx = x + direction.x();
        const int ny = y + direction.y();
        if (!inBounds(nx, ny)) {
            continue;
        }
        if (const ZoneState* zone = zoneById(m_cells.at(cellIndex(nx, ny)).zoneId)) {
            return zone;
        }
    }
    return nullptr;
}

bool DefenseGameServer::isReinforcedZoneBarrier(int x, int y, qint64 nowMs) const
{
    const ZoneState* zone = protectingZoneForCell(x, y);
    return zone && zone->state == _game_zone_permanent
        && nowMs < zone->reinforceUntilMs;
}

void DefenseGameServer::initializeMap()
{
    m_cells.clear();
    m_cells.resize(GAME_MAP_WIDTH * GAME_MAP_HEIGHT);
    m_players.clear();
    m_zombies.clear();
    m_zones.clear();
    m_repairs.clear();
    m_sessions.clear();
    m_nextZombieId = 1;
    m_nextZoneId = 1;
    m_wave = 0;
    m_nextWaveAtMs = 0;
    m_waveAssaultUntilMs = 0;
    m_nextDisasterAtMs = 0;
    m_waveResetForNoSurvivors = false;
}

void DefenseGameServer::generateMapFeatures()
{
    QRandomGenerator random(QRandomGenerator::global()->generate());
    auto available = [this](int x, int y) {
        if (!inBounds(x, y)) {
            return false;
        }
        const CellState& cell = m_cells.at(cellIndex(x, y));
        if (cell.buildingType != _game_building_none || cell.zoneId != 0) {
            return false;
        }
        for (auto player = m_players.constBegin(); player != m_players.constEnd(); ++player) {
            if (qFloor(player->x) == x && qFloor(player->y) == y) {
                return false;
            }
        }
        return true;
    };

    // 山体由多个相交椭圆组成，形成可辨识的大块山脉而不是散点障碍。
    for (int cluster = 0; cluster < 6; ++cluster) {
        const int centerX = random.bounded(8, GAME_MAP_WIDTH - 8);
        const int centerY = random.bounded(8, GAME_MAP_HEIGHT - 8);
        const int radiusX = random.bounded(4, 9);
        const int radiusY = random.bounded(3, 7);
        for (int y = centerY - radiusY; y <= centerY + radiusY; ++y) {
            for (int x = centerX - radiusX; x <= centerX + radiusX; ++x) {
                const double dx = double(x - centerX) / radiusX;
                const double dy = double(y - centerY) / radiusY;
                if (dx * dx + dy * dy > 1.0 + random.generateDouble() * 0.18
                    || !available(x, y)) {
                    continue;
                }
                CellState& cell = m_cells[cellIndex(x, y)];
                cell.terrain = _game_terrain_mountain;
                cell.resourceType = _game_resource_stone;
                cell.resourceAmount = random.bounded(260, 481);
            }
        }
    }

    QVector<QPoint> mountainEdges;
    static const QPoint directions[] = {
        QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)
    };
    for (int y = 1; y < GAME_MAP_HEIGHT - 1; ++y) {
        for (int x = 1; x < GAME_MAP_WIDTH - 1; ++x) {
            if (m_cells.at(cellIndex(x, y)).terrain == _game_terrain_mountain) {
                continue;
            }
            for (const QPoint& direction : directions) {
                if (m_cells.at(cellIndex(x + direction.x(), y + direction.y())).terrain
                    == _game_terrain_mountain) {
                    mountainEdges.append(QPoint(x, y));
                    break;
                }
            }
        }
    }
    auto placeResource = [&](int x, int y, int type, int minAmount, int maxAmount) {
        if (!available(x, y)) {
            return false;
        }
        CellState& cell = m_cells[cellIndex(x, y)];
        if (cell.terrain != _game_terrain_plain || cell.resourceType != _game_resource_none) {
            return false;
        }
        cell.resourceType = type;
        cell.resourceAmount = random.bounded(minAmount, maxAmount + 1);
        return true;
    };

    // 铝与铁多出现在山麓；铁更稀少。
    for (int i = 0; i < 34 && !mountainEdges.isEmpty(); ++i) {
        const QPoint edge = mountainEdges.at(random.bounded(mountainEdges.size()));
        placeResource(edge.x(), edge.y(), _game_resource_aluminum, 90, 170);
    }
    for (int i = 0; i < 24 && !mountainEdges.isEmpty(); ++i) {
        const QPoint edge = mountainEdges.at(random.bounded(mountainEdges.size()));
        const int x = qBound(1, edge.x() + random.bounded(-2, 3), GAME_MAP_WIDTH - 2);
        const int y = qBound(1, edge.y() + random.bounded(-2, 3), GAME_MAP_HEIGHT - 2);
        placeResource(x, y, _game_resource_iron, 80, 140);
    }
    // 煤呈短矿脉分布。
    for (int vein = 0; vein < 5; ++vein) {
        int x = random.bounded(4, GAME_MAP_WIDTH - 4);
        int y = random.bounded(4, GAME_MAP_HEIGHT - 4);
        const QPoint direction = random.bounded(2) == 0 ? QPoint(1, 0) : QPoint(0, 1);
        for (int length = 0; length < random.bounded(2, 5); ++length) {
            placeResource(x, y, _game_resource_coal, 70, 125);
            x += direction.x();
            y += direction.y();
        }
    }
    // 石油只形成少量、紧凑的小型油田。
    for (int field = 0; field < 4; ++field) {
        const int centerX = random.bounded(5, GAME_MAP_WIDTH - 5);
        const int centerY = random.bounded(5, GAME_MAP_HEIGHT - 5);
        for (int i = 0; i < random.bounded(1, 3); ++i) {
            placeResource(centerX + random.bounded(-1, 2), centerY + random.bounded(-1, 2),
                          _game_resource_oil, 55, 95);
        }
    }
}

void DefenseGameServer::processMove(PlayerState& player, float directionX, float directionY)
{
    const QPointF direction = normalizedVector(directionX, directionY);
    player.moveX = static_cast<float>(direction.x());
    player.moveY = static_cast<float>(direction.y());
    player.hasMoveTarget = false;
}

void DefenseGameServer::processMoveTarget(PlayerState& player, float targetX, float targetY)
{
    player.targetX = targetX <= 0.5f ? -1.0f
        : targetX >= GAME_MAP_WIDTH - 0.5f ? GAME_MAP_WIDTH + 1.0f
        : qBound(kPlayerRadius, targetX, GAME_MAP_WIDTH - kPlayerRadius);
    player.targetY = targetY <= 0.5f ? -1.0f
        : targetY >= GAME_MAP_HEIGHT - 0.5f ? GAME_MAP_HEIGHT + 1.0f
        : qBound(kPlayerRadius, targetY, GAME_MAP_HEIGHT - kPlayerRadius);
    player.moveX = 0.0f;
    player.moveY = 0.0f;
    player.hasMoveTarget = true;
}

void DefenseGameServer::processAttack(PlayerState& player, float targetX, float targetY, qint64 nowMs)
{
    if (nowMs - player.lastAttackAtMs < playerAttackIntervalMs(player)) {
        return;
    }
    int selectedId = 0;
    float selectedDistance = 0.75f;
    for (int zombieId : nearbyZombieIds(targetX, targetY, 0.75f)) {
        const auto it = m_zombies.constFind(zombieId);
        if (it == m_zombies.constEnd() || it->hp <= 0) {
            continue;
        }
        const float cursorDistance = distanceBetween(it->x, it->y, targetX, targetY);
        if (cursorDistance < selectedDistance) {
            selectedId = zombieId;
            selectedDistance = cursorDistance;
        }
    }
    auto selected = m_zombies.find(selectedId);
    if (selected == m_zombies.end()
        || distanceBetween(player.x, player.y, selected->x, selected->y)
            > playerAttackRange(player)) {
        return;
    }
    player.lastAttackAtMs = nowMs;
    damageZombie(selected.value(), playerAttack(player), player.userId, -1, true);
    removeDeadZombies();
}

void DefenseGameServer::processBuild(PlayerState& player, int x, int y, int buildingType)
{
    if (!inBounds(x, y) || distanceBetween(player.x, player.y, x + 0.5f, y + 0.5f) > 2.5f
        || isCellOccupiedByUnit(x, y)) {
        sendEvent(player.userId, _game_event_warning, QStringLiteral("只能在角色附近的空格建造。"));
        return;
    }
    CellState& cell = m_cells[cellIndex(x, y)];
    if (cell.buildingType != _game_building_none) {
        sendEvent(player.userId, _game_event_warning, QStringLiteral("该位置已经有建筑。"));
        return;
    }
    if (isExtractor(buildingType)) {
        const int resourceCell = extractorResourceCell(cellIndex(x, y), buildingType);
        if (cell.terrain != _game_terrain_plain || resourceCell < 0) {
            sendEvent(player.userId, _game_event_warning,
                      buildingType == _game_building_stone_extractor
                          ? QStringLiteral("采石机必须建在山峰旁的平地。")
                          : QStringLiteral("采集机必须建在对应的资源点上。"));
            return;
        }
    } else if (cell.terrain != _game_terrain_plain
               || cell.resourceType != _game_resource_none) {
        sendEvent(player.userId, _game_event_warning,
                  cell.terrain == _game_terrain_mountain
                      ? QStringLiteral("山峰不可建造和通行。")
                      : QStringLiteral("资源点只能建造对应的采集机。"));
        return;
    }
    const bool wallLike = buildingType == _game_building_wall
        || buildingType == _game_building_door;
    if (isTurretBuilding(buildingType)
        && turretPopulationUsed(player.userId) >= player.population) {
        sendEvent(player.userId, _game_event_warning,
                  QStringLiteral("没有可用人口。每座炮塔需要 1 人操作。"));
        return;
    }
    const BuildingCost cost = buildingCost(buildingType);
    const int hp = buildingMaxHp(buildingType, 1);
    if (!cost.valid || (!kUnlimitedMaterials && !canAfford(player, cost))) {
        sendEvent(player.userId, _game_event_warning,
                  QStringLiteral("资源不足，需要：%1。").arg(formatCost(cost)));
        return;
    }

    cell.buildingType = buildingType;
    cell.buildingOwnerId = player.userId;
    cell.buildingLevel = 1;
    cell.buildingExp = 0;
    cell.buildingHp = hp;
    if (!kUnlimitedMaterials) {
        applyCost(player, cost);
    }
    if (buildingType == _game_building_house) {
        const PopulationSupport support = populationSupport(player.userId);
        player.population = qMin(support.capacity,
                                 player.population + housePopulationCapacity(1));
    }
    if (wallLike) {
        detectNewZones(player.userId, QDateTime::currentMSecsSinceEpoch());
    }
}

void DefenseGameServer::processDemolish(PlayerState& player, int x, int y)
{
    if (!inBounds(x, y) || distanceBetween(player.x, player.y, x + 0.5f, y + 0.5f) > 2.5f) {
        return;
    }
    CellState& cell = m_cells[cellIndex(x, y)];
    if (cell.buildingType == _game_building_none || cell.buildingOwnerId != player.userId) {
        return;
    }
    if (!kUnlimitedMaterials) {
        BuildingCost refund = buildingCost(cell.buildingType);
        refund.coins /= 2;
        refund.stone /= 2;
        refund.aluminum /= 2;
        refund.iron /= 2;
        refund.coal /= 2;
        refund.oil /= 2;
        applyCost(player, refund, 1);
    }
    cell.buildingType = _game_building_none;
    cell.buildingOwnerId = 0;
    cell.buildingHp = 0;
    cell.buildingLevel = 0;
    cell.buildingExp = 0;
    m_turretLastAttackAtMs.remove(cellIndex(x, y));
    m_repairs.remove(cellIndex(x, y));
}

void DefenseGameServer::processUpgrade(PlayerState& player, int x, int y)
{
    if (!inBounds(x, y) || distanceBetween(player.x, player.y, x + 0.5f, y + 0.5f) > 2.5f) {
        return;
    }
    CellState& cell = m_cells[cellIndex(x, y)];
    if (cell.buildingType == _game_building_none || cell.buildingOwnerId != player.userId) {
        sendEvent(player.userId, _game_event_warning, QStringLiteral("只能升级自己的防御建筑。"));
        return;
    }
    const int level = qMax(1, cell.buildingLevel);
    const int cost = buildingUpgradeCost(level);
    const int buildingMaxLevel = cell.buildingType == _game_building_house
        ? qMin(GAME_HOUSE_MAX_LEVEL, player.level)
        : qMin(player.level, GAME_MAX_LEVEL);
    if (level >= buildingMaxLevel) {
        sendEvent(player.userId, _game_event_warning,
                  cell.buildingType == _game_building_house
                      ? QStringLiteral("房屋最高 10 级，且不能超过玩家等级。")
                      : QStringLiteral("防御建筑等级不能超过玩家等级。"));
        return;
    }
    if (player.coins < cost) {
        sendEvent(player.userId, _game_event_warning,
                  QStringLiteral("升级需要 %1 金币。").arg(cost));
        return;
    }

    player.coins -= cost;
    const int previousHouseCapacity = cell.buildingType == _game_building_house
        ? housePopulationCapacity(level) : 0;
    ++cell.buildingLevel;
    cell.buildingExp = 0;
    cell.buildingHp = buildingMaxHp(cell.buildingType, cell.buildingLevel);
    if (cell.buildingType == _game_building_house) {
        const int addedCapacity = housePopulationCapacity(cell.buildingLevel)
            - previousHouseCapacity;
        const PopulationSupport support = populationSupport(player.userId);
        player.population = qMin(support.capacity,
                                 player.population + qMax(0, addedCapacity));
    }
    sendEvent(player.userId, _game_event_success,
              QStringLiteral("建筑已升级到 %1 级，消耗 %2 金币。")
                  .arg(cell.buildingLevel).arg(cost));
}

void DefenseGameServer::processRepair(PlayerState& player, int x, int y)
{
    if (!inBounds(x, y) || distanceBetween(player.x, player.y, x + 0.5f, y + 0.5f) > 2.5f) {
        return;
    }
    CellState& cell = m_cells[cellIndex(x, y)];
    if (cell.buildingType == _game_building_none || cell.buildingOwnerId != player.userId) {
        sendEvent(player.userId, _game_event_warning, QStringLiteral("只能维修自己的防御建筑。"));
        return;
    }
    const int maxHp = buildingMaxHp(cell.buildingType, cell.buildingLevel);
    if (cell.buildingHp >= maxHp) {
        sendEvent(player.userId, _game_event_info, QStringLiteral("该防御建筑无需维修。"));
        return;
    }
    const int index = cellIndex(x, y);
    if (m_repairs.contains(index)) {
        sendEvent(player.userId, _game_event_info, QStringLiteral("该建筑正在维修中。"));
        return;
    }
    if (!kUnlimitedMaterials && player.coins <= 0) {
        sendEvent(player.userId, _game_event_warning,
                  QStringLiteral("开始维修至少需要 1 金币。"));
        return;
    }
    RepairState repair;
    repair.ownerId = player.userId;
    repair.lastTickAtMs = QDateTime::currentMSecsSinceEpoch();
    m_repairs.insert(index, repair);
    sendEvent(player.userId, _game_event_info,
              QStringLiteral("开始维修，每秒恢复 %1 耐久并按恢复量扣除金币。")
                  .arg(GAME_BUILDING_REPAIR_HP_PER_TICK));
}

void DefenseGameServer::processSetRespawnZone(PlayerState& player, int x, int y)
{
    if (!inBounds(x, y)) {
        return;
    }
    const ZoneState* zone = zoneById(m_cells.at(cellIndex(x, y)).zoneId);
    if (!zone || zone->ownerId != player.userId) {
        sendEvent(player.userId, _game_event_warning,
                  QStringLiteral("只能将自己的安全区设置为复活地盘。"));
        return;
    }
    const QPointF spawn = findSpawnPositionInZone(*zone);
    player.respawnZoneId = zone->id;
    player.spawnX = static_cast<float>(spawn.x());
    player.spawnY = static_cast<float>(spawn.y());
    sendEvent(player.userId, _game_event_success,
              QStringLiteral("已将 #%1 安全区设置为复活地盘。").arg(zone->id));
}

void DefenseGameServer::refreshPlayerRespawnLocation(PlayerState& player)
{
    int selectedZoneId = 0;
    const QPointF spawn = preferredPlayerSpawn(player.userId, player.respawnZoneId,
                                                &selectedZoneId);
    if (selectedZoneId <= 0) {
        player.respawnZoneId = 0;
        if (!inBounds(qFloor(player.spawnX), qFloor(player.spawnY))) {
            player.spawnX = static_cast<float>(spawn.x());
            player.spawnY = static_cast<float>(spawn.y());
        }
        return;
    }
    player.respawnZoneId = selectedZoneId;
    player.spawnX = static_cast<float>(spawn.x());
    player.spawnY = static_cast<float>(spawn.y());
}

void DefenseGameServer::processRepairs(qint64 nowMs)
{
    const QList<int> indices = m_repairs.keys();
    for (int index : indices) {
        auto repair = m_repairs.find(index);
        if (repair == m_repairs.end() || index < 0 || index >= m_cells.size()) {
            m_repairs.remove(index);
            continue;
        }
        PlayerState* owner = playerForUser(repair->ownerId);
        CellState& building = m_cells[index];
        const int x = index % GAME_MAP_WIDTH;
        const int y = index / GAME_MAP_WIDTH;
        if (!owner || !owner->online || owner->respawnAtMs > 0
            || building.buildingType == _game_building_none
            || building.buildingOwnerId != repair->ownerId
            || distanceBetween(owner->x, owner->y, x + 0.5f, y + 0.5f) > 2.5f) {
            if (owner && owner->online) {
                sendEvent(owner->userId, _game_event_warning,
                          QStringLiteral("维修已中断，需要留在建筑附近。"));
            }
            m_repairs.erase(repair);
            continue;
        }
        if (nowMs - repair->lastTickAtMs < GAME_BUILDING_REPAIR_INTERVAL_MS) {
            continue;
        }
        repair->lastTickAtMs = nowMs;
        const int maxHp = buildingMaxHp(building.buildingType, building.buildingLevel);
        const int restored = qMin(GAME_BUILDING_REPAIR_HP_PER_TICK,
                                  qMax(0, maxHp - building.buildingHp));
        if (restored <= 0) {
            sendEvent(owner->userId, _game_event_success, QStringLiteral("建筑维修完成。"));
            m_repairs.erase(repair);
            continue;
        }
        const int cost = (restored + GAME_BUILDING_REPAIR_HP_PER_COIN - 1)
            / GAME_BUILDING_REPAIR_HP_PER_COIN;
        if (!kUnlimitedMaterials && owner->coins < cost) {
            sendEvent(owner->userId, _game_event_warning,
                      QStringLiteral("金币不足，维修已中断。"));
            m_repairs.erase(repair);
            continue;
        }
        if (!kUnlimitedMaterials) {
            owner->coins -= cost;
        }
        building.buildingHp += restored;
        if (building.buildingHp >= maxHp) {
            building.buildingHp = maxHp;
            sendEvent(owner->userId, _game_event_success, QStringLiteral("建筑维修完成。"));
            m_repairs.erase(repair);
        }
    }
}

void DefenseGameServer::processPlayerMovement(float deltaSeconds)
{
    struct PendingTransition {
        ConnectionId socket = 0;
        qint64 userId = 0;
        int directionX = 0;
        int directionY = 0;
    };
    QVector<PendingTransition> pendingTransitions;
    for (auto it = m_players.begin(); it != m_players.end(); ++it) {
        PlayerState& player = it.value();
        if (!player.online || player.respawnAtMs > 0) {
            continue;
        }

        if (isPositionBlocked(player.x, player.y, kPlayerRadius)) {
            const QPointF safePosition = findSafePositionNear(player.x, player.y);
            player.x = static_cast<float>(safePosition.x());
            player.y = static_cast<float>(safePosition.y());
            player.moveX = 0.0f;
            player.moveY = 0.0f;
            player.hasMoveTarget = false;
            sendEvent(player.userId, _game_event_warning,
                      QStringLiteral("检测到角色被建筑卡住，已移动到附近空地。"));
            continue;
        }

        QPointF direction(player.moveX, player.moveY);
        float remainingDistance = 0.0f;
        if (player.hasMoveTarget) {
            const float dx = player.targetX - player.x;
            const float dy = player.targetY - player.y;
            remainingDistance = distanceBetween(player.x, player.y, player.targetX, player.targetY);
            direction = normalizedVector(dx, dy);
            if (remainingDistance <= 0.06f) {
                player.hasMoveTarget = false;
                continue;
            }
        }

        const float maxStep = m_playerSpeedCellsPerSecond * deltaSeconds;
        const float step = player.hasMoveTarget ? qMin(maxStep, remainingDistance) : maxStep;
        const float nextX = player.x + static_cast<float>(direction.x()) * step;
        const float nextY = player.y + static_cast<float>(direction.y()) * step;
        int transitionX = 0;
        int transitionY = 0;
        if (direction.x() < 0.0 && nextX < kPlayerRadius) {
            transitionX = -1;
        } else if (direction.x() > 0.0 && nextX > GAME_MAP_WIDTH - kPlayerRadius) {
            transitionX = 1;
        } else if (direction.y() < 0.0 && nextY < kPlayerRadius) {
            transitionY = -1;
        } else if (direction.y() > 0.0 && nextY > GAME_MAP_HEIGHT - kPlayerRadius) {
            transitionY = 1;
        }
        if ((transitionX != 0 || transitionY != 0) && m_mapTransitionHandler) {
            player.moveX = 0.0f;
            player.moveY = 0.0f;
            player.hasMoveTarget = false;
            pendingTransitions.append({m_sessions.key(player.userId, 0),
                                       player.userId, transitionX, transitionY});
            continue;
        }
        if (!isPositionBlocked(nextX, nextY, kPlayerRadius)) {
            player.x = nextX;
            player.y = nextY;
        } else if (!isPositionBlocked(nextX, player.y, kPlayerRadius)) {
            player.x = nextX;
        } else if (!isPositionBlocked(player.x, nextY, kPlayerRadius)) {
            player.y = nextY;
        } else {
            player.hasMoveTarget = false;
        }
    }

    for (const PendingTransition& transition : pendingTransitions) {
        if (transition.socket == 0 || !m_mapTransitionHandler(
                transition.socket, transition.userId,
                transition.directionX, transition.directionY)) {
            sendEvent(transition.userId, _game_event_warning,
                      QStringLiteral("无法进入该方向的战场区域。"));
        }
    }
}

void DefenseGameServer::processPlayerAutoAttacks(qint64 nowMs)
{
    for (auto player = m_players.begin(); player != m_players.end(); ++player) {
        if (!player->online || player->respawnAtMs > 0
            || nowMs - player->lastAttackAtMs < playerAttackIntervalMs(player.value())) {
            continue;
        }
        int nearestId = 0;
        const float range = playerAttackRange(player.value());
        float nearestDistance = range;
        for (int zombieId : nearbyZombieIds(player->x, player->y, range)) {
            const auto zombie = m_zombies.constFind(zombieId);
            if (zombie == m_zombies.constEnd() || zombie->hp <= 0) {
                continue;
            }
            const float distance = distanceBetween(player->x, player->y, zombie->x, zombie->y);
            if (distance <= nearestDistance) {
                nearestId = zombieId;
                nearestDistance = distance;
            }
        }
        const auto nearest = m_zombies.constFind(nearestId);
        if (nearest != m_zombies.constEnd()) {
            processAttack(player.value(), nearest->x, nearest->y, nowMs);
        }
    }
}

void DefenseGameServer::processPlayerRespawns(qint64 nowMs)
{
    for (auto player = m_players.begin(); player != m_players.end(); ++player) {
        if (player->respawnAtMs <= 0 || nowMs < player->respawnAtMs) {
            continue;
        }
        player->respawnAtMs = 0;
        player->invulnerableUntilMs = nowMs + kPlayerRespawnInvulnerabilityMs;
        player->hp = player->maxHp;
        refreshPlayerRespawnLocation(player.value());
        const QPointF respawn = findSafePositionNear(player->spawnX, player->spawnY);
        player->x = static_cast<float>(respawn.x());
        player->y = static_cast<float>(respawn.y());
        player->moveX = 0.0f;
        player->moveY = 0.0f;
        player->hasMoveTarget = false;
        player->lastAttackAtMs = nowMs;
        sendEvent(player->userId, _game_event_success,
                  player->respawnZoneId > 0
                      ? QStringLiteral("重生完成，已返回复活地盘并获得 5 秒无敌保护。")
                      : QStringLiteral("重生完成，已返回初始位置并获得 5 秒无敌保护。"));
    }
}

void DefenseGameServer::processPlayerRegeneration(qint64 nowMs)
{
    for (auto player = m_players.begin(); player != m_players.end(); ++player) {
        if (!player->online || player->respawnAtMs > 0 || player->hp <= 0
            || player->hp >= player->maxHp) {
            player->lastTerritoryRegenAtMs = nowMs;
            continue;
        }

        const int x = qBound(0, qFloor(player->x), GAME_MAP_WIDTH - 1);
        const int y = qBound(0, qFloor(player->y), GAME_MAP_HEIGHT - 1);
        const ZoneState* zone = zoneById(m_cells.at(cellIndex(x, y)).zoneId);
        if (!zone || zone->ownerId != player->userId
            || (zone->state != _game_zone_stabilizing
                && zone->state != _game_zone_permanent)) {
            player->lastTerritoryRegenAtMs = nowMs;
            continue;
        }

        if (player->lastTerritoryRegenAtMs <= 0) {
            player->lastTerritoryRegenAtMs = nowMs;
            continue;
        }
        const qint64 elapsed = nowMs - player->lastTerritoryRegenAtMs;
        const int ticks = static_cast<int>(elapsed / GAME_PLAYER_TERRITORY_REGEN_INTERVAL_MS);
        if (ticks <= 0) {
            continue;
        }
        player->hp = qMin(player->maxHp,
                          player->hp + ticks * GAME_PLAYER_TERRITORY_REGEN_HP);
        player->lastTerritoryRegenAtMs +=
            ticks * GAME_PLAYER_TERRITORY_REGEN_INTERVAL_MS;
    }
}

void DefenseGameServer::removePlayerAssets(qint64 userId)
{
    const QList<int> repairIndices = m_repairs.keys();
    for (int index : repairIndices) {
        if (m_repairs.value(index).ownerId == userId) {
            m_repairs.remove(index);
        }
    }
    for (CellState& cell : m_cells) {
        const ZoneState* zone = zoneById(cell.zoneId);
        const bool protectedByOwnedZone = zone && zone->ownerId == userId
            && (zone->state == _game_zone_stabilizing
                || zone->state == _game_zone_permanent);
        if (cell.buildingOwnerId == userId && !protectedByOwnedZone) {
            cell.buildingType = _game_building_none;
            cell.buildingOwnerId = 0;
            cell.buildingHp = 0;
            cell.buildingLevel = 0;
            cell.buildingExp = 0;
        }
    }
    m_lastFlowFieldAtMs = 0;
}

void DefenseGameServer::detectNewZones(qint64 wallOwnerId, qint64 nowMs)
{
    if (m_zones.size() >= GAME_MAX_ZONES) {
        return;
    }

    const int totalCells = GAME_MAP_WIDTH * GAME_MAP_HEIGHT;
    QVector<bool> outside(totalCells, false);
    QQueue<QPoint> queue;
    auto isOwnerWall = [this, wallOwnerId](int x, int y) {
        const CellState& cell = m_cells.at(cellIndex(x, y));
        const bool wallLike = cell.buildingType == _game_building_wall
            || cell.buildingType == _game_building_door;
        return wallLike && cell.buildingOwnerId == wallOwnerId;
    };
    auto enqueueOutside = [&](int x, int y) {
        const int index = cellIndex(x, y);
        if (!outside[index] && !isOwnerWall(x, y)) {
            outside[index] = true;
            queue.enqueue(QPoint(x, y));
        }
    };

    for (int x = 0; x < GAME_MAP_WIDTH; ++x) {
        enqueueOutside(x, 0);
        enqueueOutside(x, GAME_MAP_HEIGHT - 1);
    }
    for (int y = 0; y < GAME_MAP_HEIGHT; ++y) {
        enqueueOutside(0, y);
        enqueueOutside(GAME_MAP_WIDTH - 1, y);
    }

    static const QPoint directions[] = {QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)};
    while (!queue.isEmpty()) {
        const QPoint point = queue.dequeue();
        for (const QPoint& direction : directions) {
            const int nx = point.x() + direction.x();
            const int ny = point.y() + direction.y();
            if (inBounds(nx, ny)) {
                enqueueOutside(nx, ny);
            }
        }
    }

    QVector<bool> visited(totalCells, false);
    for (int y = 0; y < GAME_MAP_HEIGHT && m_zones.size() < GAME_MAX_ZONES; ++y) {
        for (int x = 0; x < GAME_MAP_WIDTH && m_zones.size() < GAME_MAX_ZONES; ++x) {
            const int startIndex = cellIndex(x, y);
            if (outside[startIndex] || visited[startIndex] || isOwnerWall(x, y)) {
                continue;
            }

            QSet<int> component;
            bool containsExistingZone = false;
            QQueue<QPoint> componentQueue;
            componentQueue.enqueue(QPoint(x, y));
            visited[startIndex] = true;
            while (!componentQueue.isEmpty()) {
                const QPoint point = componentQueue.dequeue();
                const int index = cellIndex(point.x(), point.y());
                component.insert(index);
                containsExistingZone = containsExistingZone || m_cells.at(index).zoneId != 0;
                for (const QPoint& direction : directions) {
                    const int nx = point.x() + direction.x();
                    const int ny = point.y() + direction.y();
                    if (!inBounds(nx, ny)) {
                        continue;
                    }
                    const int nextIndex = cellIndex(nx, ny);
                    if (!outside[nextIndex] && !visited[nextIndex] && !isOwnerWall(nx, ny)) {
                        visited[nextIndex] = true;
                        componentQueue.enqueue(QPoint(nx, ny));
                    }
                }
            }

            if (containsExistingZone || component.isEmpty()) {
                continue;
            }
            ZoneState zone;
            zone.id = m_nextZoneId++;
            zone.ownerId = wallOwnerId;
            if (const PlayerState* owner = playerForUser(wallOwnerId)) {
                zone.color = owner->color;
            }
            zone.state = _game_zone_stabilizing;
            zone.claimedAtMs = nowMs;
            zone.stabilizeAtMs = nowMs + kStabilizationMs;
            QSet<int> claimedCells = component;
            static const QPoint boundaryDirections[] = {
                QPoint(-1, -1), QPoint(0, -1), QPoint(1, -1), QPoint(-1, 0),
                QPoint(1, 0), QPoint(-1, 1), QPoint(0, 1), QPoint(1, 1)
            };
            for (int index : component) {
                const int cellX = index % GAME_MAP_WIDTH;
                const int cellY = index / GAME_MAP_WIDTH;
                for (const QPoint& direction : boundaryDirections) {
                    const int boundaryX = cellX + direction.x();
                    const int boundaryY = cellY + direction.y();
                    if (inBounds(boundaryX, boundaryY)
                        && isOwnerWall(boundaryX, boundaryY)
                        && m_cells.at(cellIndex(boundaryX, boundaryY)).zoneId == 0) {
                        claimedCells.insert(cellIndex(boundaryX, boundaryY));
                    }
                }
            }
            zone.cells = claimedCells;
            for (int index : claimedCells) {
                m_cells[index].zoneId = zone.id;
            }
            m_zones.insert(zone.id, zone);
            if (PlayerState* owner = playerForUser(wallOwnerId)) {
                refreshPlayerRespawnLocation(*owner);
            }
            sendEvent(wallOwnerId, _game_event_success,
                      QStringLiteral("闭合防线已形成，含外墙的 %1 格安全区进入 10 分钟稳固期。")
                          .arg(claimedCells.size()));
        }
    }
}

void DefenseGameServer::promoteStableZones(qint64 nowMs)
{
    for (auto it = m_zones.begin(); it != m_zones.end(); ++it) {
        ZoneState& zone = it.value();
        if (zone.state == _game_zone_stabilizing && nowMs >= zone.stabilizeAtMs) {
            zone.state = _game_zone_permanent;
            zone.shieldLayers = 3;
            zone.reinforceUntilMs = 0;
            sendEvent(zone.ownerId, _game_event_success,
                      QStringLiteral("安全区已完成稳固，获得三层永久区护盾。"));
        }
    }
}

bool DefenseGameServer::isZoneFullyEnclosed(const ZoneState& zone) const
{
    if (zone.cells.isEmpty()) {
        return false;
    }
    static const QPoint directions[] = {
        QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)
    };
    for (int index : zone.cells) {
        const int x = index % GAME_MAP_WIDTH;
        const int y = index / GAME_MAP_WIDTH;
        const CellState& current = m_cells.at(index);
        const bool currentBarrier = current.buildingOwnerId == zone.ownerId
            && (current.buildingType == _game_building_wall
                || current.buildingType == _game_building_door);
        if (currentBarrier) {
            continue;
        }
        for (const QPoint& direction : directions) {
            const int nx = x + direction.x();
            const int ny = y + direction.y();
            if (!inBounds(nx, ny)) {
                return false;
            }
            const CellState& neighbor = m_cells.at(cellIndex(nx, ny));
            if (neighbor.zoneId == zone.id) {
                continue;
            }
            const bool ownerBarrier = neighbor.buildingOwnerId == zone.ownerId
                && (neighbor.buildingType == _game_building_wall
                    || neighbor.buildingType == _game_building_door);
            if (!ownerBarrier) {
                return false;
            }
        }
    }
    return true;
}

void DefenseGameServer::validateStabilizingZones()
{
    QList<int> invalidZoneIds;
    for (auto zone = m_zones.constBegin(); zone != m_zones.constEnd(); ++zone) {
        if (zone->state == _game_zone_stabilizing && !isZoneFullyEnclosed(zone.value())) {
            invalidZoneIds.append(zone->id);
        }
    }
    for (int zoneId : invalidZoneIds) {
        auto zone = m_zones.find(zoneId);
        if (zone == m_zones.end()) {
            continue;
        }
        const qint64 ownerId = zone->ownerId;
        for (int index : zone->cells) {
            if (index >= 0 && index < m_cells.size() && m_cells[index].zoneId == zoneId) {
                m_cells[index].zoneId = 0;
            }
        }
        m_zones.erase(zone);
        if (PlayerState* owner = playerForUser(ownerId)) {
            refreshPlayerRespawnLocation(*owner);
        }
        sendEvent(ownerId, _game_event_warning,
                  QStringLiteral("稳固期安全区已失去完整围墙，领地失效。重新闭合后将重新开始稳固。"));
    }
}

void DefenseGameServer::processTurrets(qint64 nowMs)
{
    for (int y = 0; y < GAME_MAP_HEIGHT; ++y) {
        for (int x = 0; x < GAME_MAP_WIDTH; ++x) {
            const int turretIndex = cellIndex(x, y);
            CellState& cell = m_cells[turretIndex];
            if (cell.buildingType != _game_building_turret
                && cell.buildingType != _game_building_heavy_turret) {
                continue;
            }
            if (nowMs - m_turretLastAttackAtMs.value(turretIndex, 0)
                < turretFireIntervalMs(cell.buildingType)) {
                continue;
            }
            int bestId = 0;
            const float range = turretRange(cell.buildingType, cell.buildingLevel);
            float bestDistance = range + 1.0f;
            for (int zombieId : nearbyZombieIds(x + 0.5f, y + 0.5f, range)) {
                const auto zombie = m_zombies.constFind(zombieId);
                if (zombie == m_zombies.constEnd() || zombie->hp <= 0) {
                    continue;
                }
                const float distance = distanceBetween(x + 0.5f, y + 0.5f, zombie->x, zombie->y);
                if (distance <= range && distance < bestDistance) {
                    bestId = zombieId;
                    bestDistance = distance;
                }
            }
            auto best = m_zombies.find(bestId);
            if (best != m_zombies.end()) {
                m_turretLastAttackAtMs[turretIndex] = nowMs;
                const int primaryZombieId = best->id;
                const float impactX = best->x;
                const float impactY = best->y;
                damageZombie(best.value(), turretAttack(cell.buildingType, cell.buildingLevel),
                             cell.buildingOwnerId, turretIndex, false);
                applyTurretSplashDamage(cell, turretIndex, primaryZombieId,
                                        impactX, impactY);
            }
        }
    }
}

void DefenseGameServer::applyTurretSplashDamage(const CellState& turret, int turretIndex,
                                                int primaryZombieId, float impactX,
                                                float impactY)
{
    if (turret.buildingType != _game_building_turret
        || turret.buildingLevel < GAME_TURRET_SPLASH_UNLOCK_LEVEL) {
        return;
    }

    const int splashDamage = qMax(1,
        turretAttack(turret.buildingType, turret.buildingLevel)
            * GAME_TURRET_SPLASH_DAMAGE_PERCENT / 100);
    const QVector<int> nearby = nearbyZombieIds(
        impactX, impactY, GAME_TURRET_SPLASH_RADIUS);
    for (int zombieId : nearby) {
        if (zombieId == primaryZombieId) {
            continue;
        }
        auto zombie = m_zombies.find(zombieId);
        if (zombie == m_zombies.end() || zombie->hp <= 0
            || distanceBetween(impactX, impactY, zombie->x, zombie->y)
                > GAME_TURRET_SPLASH_RADIUS) {
            continue;
        }
        damageZombie(zombie.value(), splashDamage, turret.buildingOwnerId,
                     turretIndex, false);
    }
    sendEvent(0, _game_event_turret_explosion, QString(), impactX, impactY);
}

void DefenseGameServer::processExtractors(qint64 nowMs)
{
    Q_UNUSED(nowMs)
    for (int index = 0; index < m_cells.size(); ++index) {
        CellState& building = m_cells[index];
        if (!isProductionBuilding(building.buildingType)) {
            continue;
        }
        PlayerState* owner = playerForUser(building.buildingOwnerId);
        if (!owner) {
            continue;
        }
        if (building.buildingType == _game_building_coin_collector) {
            owner->coins += GAME_COIN_COLLECTOR_YIELD
                * qMax(1, building.buildingLevel);
            continue;
        }
        const int resourceIndex = extractorResourceCell(index, building.buildingType);
        if (resourceIndex < 0) {
            continue;
        }
        CellState& resource = m_cells[resourceIndex];
        const int baseYield = resource.resourceType == _game_resource_stone ? 3
            : resource.resourceType == _game_resource_aluminum ? 2
            : resource.resourceType == _game_resource_iron ? 2 : 1;
        const int extracted = qMin(resource.resourceAmount,
                                   baseYield * qMax(1, building.buildingLevel));
        if (extracted <= 0) {
            continue;
        }
        resource.resourceAmount -= extracted;
        switch (resource.resourceType) {
        case _game_resource_stone: owner->stone += extracted; break;
        case _game_resource_aluminum: owner->aluminum += extracted; break;
        case _game_resource_iron: owner->iron += extracted; break;
        case _game_resource_coal: owner->coal += extracted; break;
        case _game_resource_oil: owner->oil += extracted; break;
        default: break;
        }
        if (resource.resourceAmount <= 0 && resource.terrain != _game_terrain_mountain) {
            resource.resourceType = _game_resource_none;
        }
    }
}

void DefenseGameServer::processPopulation(qint64 nowMs)
{
    Q_UNUSED(nowMs)
    for (auto player = m_players.begin(); player != m_players.end(); ++player) {
        const PopulationSupport support = populationSupport(player->userId);
        const int sustainablePopulation = qMin(support.capacity, qMin(support.food, support.water));
        if (player->population > sustainablePopulation) {
            --player->population;
            if (player->online) {
                sendEvent(player->userId, _game_event_warning,
                          QStringLiteral("人口供给不足，当前人口 %1 / 可维持 %2。")
                              .arg(player->population).arg(sustainablePopulation));
            }
        } else if (player->population < sustainablePopulation) {
            ++player->population;
            if (player->online) {
                sendEvent(player->userId, _game_event_success,
                          QStringLiteral("人口增长至 %1。").arg(player->population));
            }
        }
    }
}

void DefenseGameServer::damageZombie(ZombieState& zombie, int damage, qint64 ownerId,
                                     int buildingIndex, bool playerDamage)
{
    if (zombie.hp <= 0 || damage <= 0) {
        return;
    }
    zombie.hp -= damage;
    zombie.lastDamageOwnerId = ownerId;
    zombie.lastDamageBuildingIndex = buildingIndex;
    zombie.lastDamageWasPlayer = playerDamage;
    if (zombie.hp > 0) {
        return;
    }
    if (zombie.kind == GAME_ZOMBIE_KIND_GIANT) {
        explodeGiantZombie(zombie, QDateTime::currentMSecsSinceEpoch());
    }

    PlayerState* owner = playerForUser(ownerId);
    if (!owner) {
        return;
    }
    const int reward = GAME_ZOMBIE_COIN_REWARD
        + zombie.kind * GAME_ZOMBIE_ELITE_COIN_BONUS;
    owner->coins += reward;
    ++owner->kills;
    if (playerDamage) {
        const int gainedExperience = GAME_PLAYER_KILL_EXP
            + zombie.kind * GAME_PLAYER_ELITE_EXP_BONUS;
        owner->experience += gainedExperience;
        bool leveledUp = false;
        while (owner->level < GAME_MAX_LEVEL
               && owner->experience >= playerExperienceRequired(owner->level)) {
            owner->experience -= playerExperienceRequired(owner->level);
            ++owner->level;
            leveledUp = true;
        }
        if (owner->level >= GAME_MAX_LEVEL) {
            owner->experience = 0;
        }
        sendEvent(ownerId, _game_event_success,
                  leveledUp
                      ? QStringLiteral("击杀僵尸获得 %1 金币和 %2 经验，玩家升到 %3 级。")
                            .arg(reward).arg(gainedExperience).arg(owner->level)
                      : QStringLiteral("击杀僵尸获得 %1 金币和 %2 经验。")
                            .arg(reward).arg(gainedExperience));
        return;
    }

    if (buildingIndex < 0 || buildingIndex >= m_cells.size()) {
        return;
    }
    CellState& building = m_cells[buildingIndex];
    if (building.buildingType == _game_building_none || building.buildingOwnerId != ownerId) {
        return;
    }
    const int required = buildingExpRequired(building.buildingLevel);
    building.buildingExp = qMin(required, building.buildingExp + 1 + zombie.kind);
}

void DefenseGameServer::explodeGiantZombie(const ZombieState& zombie, qint64 nowMs)
{
    for (auto player = m_players.begin(); player != m_players.end(); ++player) {
        if (player->online && player->respawnAtMs <= 0
            && distanceBetween(zombie.x, zombie.y, player->x, player->y)
                <= GAME_GIANT_ZOMBIE_EXPLOSION_RADIUS
            && waveShieldAt(player->x, player->y) < 0) {
            damagePlayer(player.value(), GAME_GIANT_ZOMBIE_EXPLOSION_PLAYER_DAMAGE, nowMs);
        }
    }

    ZombieState explosionSource = zombie;
    const int minX = qMax(0, qFloor(zombie.x - GAME_GIANT_ZOMBIE_EXPLOSION_RADIUS));
    const int maxX = qMin(GAME_MAP_WIDTH - 1,
                          qFloor(zombie.x + GAME_GIANT_ZOMBIE_EXPLOSION_RADIUS));
    const int minY = qMax(0, qFloor(zombie.y - GAME_GIANT_ZOMBIE_EXPLOSION_RADIUS));
    const int maxY = qMin(GAME_MAP_HEIGHT - 1,
                          qFloor(zombie.y + GAME_GIANT_ZOMBIE_EXPLOSION_RADIUS));
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            if (m_cells.at(cellIndex(x, y)).buildingType == _game_building_none
                || distanceBetween(zombie.x, zombie.y, x + 0.5f, y + 0.5f)
                    > GAME_GIANT_ZOMBIE_EXPLOSION_RADIUS
                || waveShieldAt(x + 0.5f, y + 0.5f) >= 0) {
                continue;
            }
            damageBuilding(explosionSource, x, y,
                           GAME_GIANT_ZOMBIE_EXPLOSION_BUILDING_DAMAGE, nowMs);
        }
    }
    sendEvent(0, _game_event_explosion, QString(), zombie.x, zombie.y);
}

int DefenseGameServer::zombieAttack(int kind) const
{
    return kind == GAME_ZOMBIE_KIND_GIANT
        ? GAME_GIANT_ZOMBIE_ATTACK
        : GAME_ZOMBIE_BASE_ATTACK + kind * GAME_ZOMBIE_ELITE_ATTACK_BONUS;
}

float DefenseGameServer::zombieCollisionRadius(int kind) const
{
    return kind == GAME_ZOMBIE_KIND_GIANT
        ? kGiantZombieCollisionRadius : kZombieCollisionRadius;
}

float DefenseGameServer::zombieSpeedMultiplier(int kind) const
{
    return kind == GAME_ZOMBIE_KIND_GIANT ? 0.58f
        : kind == GAME_ZOMBIE_KIND_ELITE ? 1.08f : 1.0f;
}

int DefenseGameServer::buildingMaxHp(int buildingType, int level) const
{
    const int baseHp = buildingType == _game_building_turret ? kTurretHp
        : buildingType == _game_building_heavy_turret ? GAME_HEAVY_TURRET_MAX_HP
        : buildingType == _game_building_house ? GAME_HOUSE_MAX_HP
        : buildingType == _game_building_farm ? GAME_FARM_MAX_HP
        : buildingType == _game_building_well ? GAME_WELL_MAX_HP
        : buildingType == _game_building_wave_shield ? GAME_WAVE_SHIELD_MAX_HP
        : isProductionBuilding(buildingType) ? GAME_EXTRACTOR_MAX_HP
        : kWallHp;
    return baseHp + baseHp * qMax(0, level - 1) * GAME_BUILDING_HP_PER_LEVEL_PERCENT / 100;
}

int DefenseGameServer::buildingExpRequired(int level) const
{
    return GAME_BUILDING_EXP_BASE + qMax(1, level) * GAME_BUILDING_EXP_PER_LEVEL;
}

int DefenseGameServer::buildingUpgradeCost(int level) const
{
    return GAME_BUILDING_UPGRADE_BASE_COST
        + qMax(1, level) * GAME_BUILDING_UPGRADE_COST_PER_LEVEL;
}

int DefenseGameServer::buildingRepairCost(const CellState& building) const
{
    const int missingHp = qMax(0, buildingMaxHp(building.buildingType, building.buildingLevel)
        - building.buildingHp);
    return (missingHp + GAME_BUILDING_REPAIR_HP_PER_COIN - 1)
        / GAME_BUILDING_REPAIR_HP_PER_COIN;
}

DefenseGameServer::BuildingCost DefenseGameServer::buildingCost(int buildingType) const
{
    BuildingCost cost;
    cost.valid = true;
    switch (buildingType) {
    case _game_building_wall:
        cost.coins = GAME_WALL_COIN_COST;
        cost.stone = GAME_WALL_STONE_COST;
        break;
    case _game_building_door:
        cost.coins = GAME_DOOR_COIN_COST;
        cost.stone = GAME_DOOR_STONE_COST;
        break;
    case _game_building_turret:
        cost.coins = GAME_TURRET_COIN_COST;
        break;
    case _game_building_heavy_turret:
        cost.coins = GAME_HEAVY_TURRET_COIN_COST;
        cost.iron = GAME_HEAVY_TURRET_IRON_COST;
        cost.coal = GAME_HEAVY_TURRET_COAL_COST;
        cost.oil = GAME_HEAVY_TURRET_OIL_COST;
        break;
    case _game_building_stone_extractor:
        cost.coins = GAME_STONE_EXTRACTOR_COIN_COST;
        break;
    case _game_building_aluminum_extractor:
        cost.coins = GAME_ALUMINUM_EXTRACTOR_COIN_COST;
        cost.stone = GAME_ALUMINUM_EXTRACTOR_STONE_COST;
        break;
    case _game_building_iron_extractor:
        cost.coins = GAME_IRON_EXTRACTOR_COIN_COST;
        cost.aluminum = GAME_IRON_EXTRACTOR_ALUMINUM_COST;
        break;
    case _game_building_coal_extractor:
        cost.coins = GAME_COAL_EXTRACTOR_COIN_COST;
        cost.iron = GAME_COAL_EXTRACTOR_IRON_COST;
        break;
    case _game_building_oil_extractor:
        cost.coins = GAME_OIL_EXTRACTOR_COIN_COST;
        cost.coal = GAME_OIL_EXTRACTOR_COAL_COST;
        break;
    case _game_building_coin_collector:
        cost.coins = GAME_COIN_COLLECTOR_COIN_COST;
        cost.stone = GAME_COIN_COLLECTOR_STONE_COST;
        break;
    case _game_building_house:
        cost.coins = GAME_HOUSE_COIN_COST;
        cost.stone = GAME_HOUSE_STONE_COST;
        break;
    case _game_building_farm:
        cost.coins = GAME_FARM_COIN_COST;
        break;
    case _game_building_well:
        cost.coins = GAME_WELL_COIN_COST;
        cost.stone = GAME_WELL_STONE_COST;
        break;
    case _game_building_wave_shield:
        cost.coins = GAME_WAVE_SHIELD_COIN_COST;
        cost.aluminum = GAME_WAVE_SHIELD_ALUMINUM_COST;
        cost.iron = GAME_WAVE_SHIELD_IRON_COST;
        cost.coal = GAME_WAVE_SHIELD_COAL_COST;
        cost.oil = GAME_WAVE_SHIELD_OIL_COST;
        break;
    default:
        cost.valid = false;
        break;
    }
    return cost;
}

bool DefenseGameServer::canAfford(const PlayerState& player, const BuildingCost& cost) const
{
    return cost.valid && player.coins >= cost.coins && player.stone >= cost.stone
        && player.aluminum >= cost.aluminum && player.iron >= cost.iron
        && player.coal >= cost.coal && player.oil >= cost.oil;
}

void DefenseGameServer::applyCost(PlayerState& player, const BuildingCost& cost, int multiplier)
{
    player.coins += cost.coins * multiplier;
    player.stone += cost.stone * multiplier;
    player.aluminum += cost.aluminum * multiplier;
    player.iron += cost.iron * multiplier;
    player.coal += cost.coal * multiplier;
    player.oil += cost.oil * multiplier;
}

QString DefenseGameServer::formatCost(const BuildingCost& cost) const
{
    QStringList parts;
    if (cost.coins) parts << QStringLiteral("%1 金币").arg(cost.coins);
    if (cost.stone) parts << QStringLiteral("%1 石头").arg(cost.stone);
    if (cost.aluminum) parts << QStringLiteral("%1 铝").arg(cost.aluminum);
    if (cost.iron) parts << QStringLiteral("%1 铁").arg(cost.iron);
    if (cost.coal) parts << QStringLiteral("%1 煤").arg(cost.coal);
    if (cost.oil) parts << QStringLiteral("%1 石油").arg(cost.oil);
    return parts.isEmpty() ? QStringLiteral("不可建造") : parts.join(QStringLiteral(" + "));
}

bool DefenseGameServer::isExtractor(int buildingType) const
{
    return buildingType >= _game_building_stone_extractor
        && buildingType <= _game_building_oil_extractor;
}

bool DefenseGameServer::isProductionBuilding(int buildingType) const
{
    return isExtractor(buildingType)
        || buildingType == _game_building_coin_collector;
}

bool DefenseGameServer::isPopulationBuilding(int buildingType) const
{
    return buildingType == _game_building_house
        || buildingType == _game_building_farm
        || buildingType == _game_building_well;
}

bool DefenseGameServer::isTurretBuilding(int buildingType) const
{
    return buildingType == _game_building_turret
        || buildingType == _game_building_heavy_turret;
}

int DefenseGameServer::turretPopulationUsed(qint64 userId) const
{
    int used = 0;
    for (const CellState& building : m_cells) {
        if (building.buildingOwnerId == userId && building.buildingHp > 0
            && isTurretBuilding(building.buildingType)) {
            ++used;
        }
    }
    return used;
}

int DefenseGameServer::waveShieldAt(float x, float y) const
{
    const int radius = qCeil(GAME_WAVE_SHIELD_RADIUS);
    const int centerX = qFloor(x);
    const int centerY = qFloor(y);
    int nearestIndex = -1;
    float nearestDistance = std::numeric_limits<float>::max();
    for (int cellY = qMax(0, centerY - radius);
         cellY <= qMin(GAME_MAP_HEIGHT - 1, centerY + radius); ++cellY) {
        for (int cellX = qMax(0, centerX - radius);
             cellX <= qMin(GAME_MAP_WIDTH - 1, centerX + radius); ++cellX) {
            const int index = cellIndex(cellX, cellY);
            const CellState& building = m_cells.at(index);
            if (building.buildingType != _game_building_wave_shield
                || building.buildingHp <= 0) {
                continue;
            }
            const float distance = distanceBetween(x, y, cellX + 0.5f, cellY + 0.5f);
            if (distance <= GAME_WAVE_SHIELD_RADIUS && distance < nearestDistance) {
                nearestIndex = index;
                nearestDistance = distance;
            }
        }
    }
    return nearestIndex;
}

int DefenseGameServer::housePopulationCapacity(int level) const
{
    level = qBound(1, level, GAME_HOUSE_MAX_LEVEL);
    return level >= 7 ? 4 : level >= 4 ? 3 : 2;
}

DefenseGameServer::PopulationSupport DefenseGameServer::populationSupport(qint64 userId) const
{
    PopulationSupport support;
    for (const CellState& building : m_cells) {
        if (building.buildingOwnerId != userId || building.buildingHp <= 0) {
            continue;
        }
        const int level = qMax(1, building.buildingLevel);
        if (building.buildingType == _game_building_house) {
            support.capacity += housePopulationCapacity(level);
        } else if (building.buildingType == _game_building_farm) {
            support.food += GAME_FARM_BASE_PEOPLE
                + (level - 1) * GAME_FARM_PEOPLE_PER_LEVEL;
        } else if (building.buildingType == _game_building_well) {
            support.water += GAME_WELL_BASE_PEOPLE
                + (level - 1) * GAME_WELL_PEOPLE_PER_LEVEL;
        }
    }
    return support;
}

int DefenseGameServer::extractorResourceType(int buildingType) const
{
    return isExtractor(buildingType)
        ? _game_resource_stone + buildingType - _game_building_stone_extractor
        : _game_resource_none;
}

int DefenseGameServer::extractorResourceCell(int buildingIndex, int buildingType) const
{
    if (buildingIndex < 0 || buildingIndex >= m_cells.size() || !isExtractor(buildingType)) {
        return -1;
    }
    const int required = extractorResourceType(buildingType);
    if (buildingType != _game_building_stone_extractor) {
        const CellState& cell = m_cells.at(buildingIndex);
        return cell.resourceType == required && cell.resourceAmount > 0 ? buildingIndex : -1;
    }
    const int x = buildingIndex % GAME_MAP_WIDTH;
    const int y = buildingIndex / GAME_MAP_WIDTH;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if ((dx == 0 && dy == 0) || !inBounds(x + dx, y + dy)) {
                continue;
            }
            const int index = cellIndex(x + dx, y + dy);
            const CellState& cell = m_cells.at(index);
            if (cell.terrain == _game_terrain_mountain
                && cell.resourceType == _game_resource_stone && cell.resourceAmount > 0) {
                return index;
            }
        }
    }
    return -1;
}

int DefenseGameServer::playerAttack(const PlayerState& player) const
{
    return GAME_PLAYER_ATTACK + qMax(0, player.level - 1) * GAME_PLAYER_ATTACK_PER_LEVEL;
}

float DefenseGameServer::playerAttackRange(const PlayerState& player) const
{
    return GAME_PLAYER_ATTACK_RANGE
        + (qMax(0, player.level - 1) / GAME_PLAYER_RANGE_LEVEL_STEP)
            * GAME_PLAYER_RANGE_PER_STEP;
}

qint64 DefenseGameServer::playerAttackIntervalMs(const PlayerState& player) const
{
    return qMax<qint64>(GAME_PLAYER_MIN_FIRE_INTERVAL_MS,
        GAME_PLAYER_FIRE_INTERVAL_MS
            - qMax(0, player.level - 1) * GAME_PLAYER_FIRE_INTERVAL_REDUCTION_PER_LEVEL_MS);
}

int DefenseGameServer::playerExperienceRequired(int level) const
{
    return GAME_PLAYER_EXP_BASE + qMax(1, level) * GAME_PLAYER_EXP_PER_LEVEL;
}

int DefenseGameServer::turretAttack(int buildingType, int level) const
{
    if (buildingType == _game_building_heavy_turret) {
        return GAME_HEAVY_TURRET_ATTACK
            + qMax(0, level - 1) * GAME_HEAVY_TURRET_ATTACK_PER_LEVEL;
    }
    return GAME_TURRET_ATTACK + qMax(0, level - 1) * GAME_TURRET_ATTACK_PER_LEVEL;
}

float DefenseGameServer::turretRange(int buildingType, int level) const
{
    if (buildingType == _game_building_heavy_turret) {
        return GAME_HEAVY_TURRET_RANGE
            + qMax(0, level - 1) / GAME_HEAVY_TURRET_RANGE_LEVEL_STEP;
    }
    return GAME_TURRET_RANGE;
}

qint64 DefenseGameServer::turretFireIntervalMs(int buildingType) const
{
    return buildingType == _game_building_heavy_turret
        ? GAME_HEAVY_TURRET_FIRE_INTERVAL_MS : GAME_TURRET_FIRE_INTERVAL_MS;
}

void DefenseGameServer::processZombies(qint64 nowMs, float deltaSeconds)
{
    QVector<QPair<float, int>> scheduled;
    scheduled.reserve(m_zombies.size());
    for (auto zombie = m_zombies.constBegin(); zombie != m_zombies.constEnd(); ++zombie) {
        if (zombie->hp > 0) {
            scheduled.append({distanceToNearestPlayerForce(zombie->x, zombie->y), zombie->id});
        }
    }
    std::sort(scheduled.begin(), scheduled.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });

    int budget = kZombieAiBudgetPerTick;
    int pathRebuildBudget = kZombiePathRebuildBudgetPerTick;
    for (const auto& item : scheduled) {
        if (budget <= 0) {
            break;
        }
        auto zombie = m_zombies.find(item.second);
        if (zombie == m_zombies.end()) {
            continue;
        }
        const float forceDistance = item.first;
        const qint64 intervalMs = zombie->wasObserved ? kTickIntervalMs
            : forceDistance <= kZombieNearSimulationDistance ? kTickIntervalMs
            : forceDistance <= kZombieMidSimulationDistance ? 250 : 1000;
        if (zombie->lastAiUpdateAtMs > 0
            && nowMs - zombie->lastAiUpdateAtMs < intervalMs) {
            continue;
        }
        const float simulationDelta = zombie->lastAiUpdateAtMs > 0
            ? qBound(0.001f, static_cast<float>(nowMs - zombie->lastAiUpdateAtMs) / 1000.0f,
                     1.0f)
            : deltaSeconds;
        zombie->lastAiUpdateAtMs = nowMs;
        const float startX = zombie->x;
        const float startY = zombie->y;
        if (zombie->wasObserved || waveShieldAt(zombie->x, zombie->y) >= 0) {
            const bool needsPath = zombiePathNeedsUpdate(zombie.value(), nowMs);
            const bool mayRebuildPath = !needsPath || pathRebuildBudget > 0;
            if (needsPath && pathRebuildBudget > 0) {
                --pathRebuildBudget;
            }
            moveZombie(zombie.value(), nowMs, simulationDelta, mayRebuildPath);
        } else {
            moveZombieWithFlowField(zombie.value(), nowMs, simulationDelta);
        }
        const float desiredX = zombie->x - startX;
        const float desiredY = zombie->y - startY;
        const float desiredLength = qSqrt(desiredX * desiredX + desiredY * desiredY);
        if (desiredLength > 0.0001f) {
            const float speed = m_zombieSpeedCellsPerSecond
                * zombieSpeedMultiplier(zombie->kind);
            zombie->preferredVelocityX = desiredX / desiredLength * speed;
            zombie->preferredVelocityY = desiredY / desiredLength * speed;
        } else {
            zombie->preferredVelocityX = 0.0f;
            zombie->preferredVelocityY = 0.0f;
        }
        zombie->x = startX;
        zombie->y = startY;
        --budget;
    }
    applyZombieCrowdAvoidance(nowMs, deltaSeconds);
}

void DefenseGameServer::applyZombieCrowdAvoidance(qint64 nowMs, float deltaSeconds)
{
    if (m_zombies.isEmpty() || deltaSeconds <= 0.0f) {
        return;
    }

    if (m_lastCrowdAvoidanceAtMs == 0
        || nowMs - m_lastCrowdAvoidanceAtMs >= kCrowdAvoidanceRefreshMs) {
        const float avoidanceDelta = m_lastCrowdAvoidanceAtMs > 0
            ? qBound(0.001f,
                     static_cast<float>(nowMs - m_lastCrowdAvoidanceAtMs) / 1000.0f,
                     0.2f)
            : deltaSeconds;
        m_lastCrowdAvoidanceAtMs = nowMs;

        RVO::RVOSimulator simulator;
        simulator.setTimeStep(avoidanceDelta);
        QVector<int> zombieIds;
        QVector<std::size_t> agentIds;
        zombieIds.reserve(m_zombies.size());
        agentIds.reserve(m_zombies.size());

        for (auto zombie = m_zombies.constBegin(); zombie != m_zombies.constEnd(); ++zombie) {
            if (zombie->hp <= 0) {
                continue;
            }
            const float radius = zombieCollisionRadius(zombie->kind);
            const float maxSpeed = m_zombieSpeedCellsPerSecond
                * zombieSpeedMultiplier(zombie->kind);
            const std::size_t agentId = simulator.addAgent(
                RVO::Vector2(zombie->x, zombie->y),
                kRvoNeighborDistance,
                kRvoMaxNeighbors,
                kRvoTimeHorizon,
                kRvoObstacleTimeHorizon,
                radius,
                maxSpeed,
                RVO::Vector2(zombie->velocityX, zombie->velocityY));
            simulator.setAgentPrefVelocity(
                agentId, RVO::Vector2(zombie->preferredVelocityX,
                                      zombie->preferredVelocityY));
            zombieIds.append(zombie.key());
            agentIds.append(agentId);
        }

        simulator.doStep();
        for (int i = 0; i < zombieIds.size(); ++i) {
            auto zombie = m_zombies.find(zombieIds.at(i));
            if (zombie == m_zombies.end()) {
                continue;
            }
            const RVO::Vector2 velocity = simulator.getAgentVelocity(agentIds.at(i));
            zombie->velocityX = velocity.x();
            zombie->velocityY = velocity.y();
        }
    }

    for (auto zombie = m_zombies.begin(); zombie != m_zombies.end(); ++zombie) {
        if (zombie->hp <= 0) {
            continue;
        }
        const float candidateX = zombie->x + zombie->velocityX * deltaSeconds;
        const float candidateY = zombie->y + zombie->velocityY * deltaSeconds;
        const int cellX = qBound(0, qFloor(candidateX), GAME_MAP_WIDTH - 1);
        const int cellY = qBound(0, qFloor(candidateY), GAME_MAP_HEIGHT - 1);
        if (!isZombiePositionBlocked(candidateX, candidateY,
                                     zombieCollisionRadius(zombie->kind))
            && !isReinforcedZoneBarrier(cellX, cellY,
                                        QDateTime::currentMSecsSinceEpoch())) {
            zombie->x = candidateX;
            zombie->y = candidateY;
            if (qAbs(zombie->velocityX) + qAbs(zombie->velocityY) > 0.001f) {
                zombie->lastProgressX = zombie->x;
                zombie->lastProgressY = zombie->y;
            }
            breachZoneCell(cellX, cellY);
        } else {
            zombie->velocityX = 0.0f;
            zombie->velocityY = 0.0f;
        }
    }
}

bool DefenseGameServer::isZombiePositionBlocked(float x, float y, float radius) const
{
    if (x < radius || y < radius || x > GAME_MAP_WIDTH - radius
        || y > GAME_MAP_HEIGHT - radius) {
        return true;
    }
    const int minX = qMax(0, qFloor(x - radius));
    const int maxX = qMin(GAME_MAP_WIDTH - 1, qFloor(x + radius));
    const int minY = qMax(0, qFloor(y - radius));
    const int maxY = qMin(GAME_MAP_HEIGHT - 1, qFloor(y + radius));
    for (int cellY = minY; cellY <= maxY; ++cellY) {
        for (int cellX = minX; cellX <= maxX; ++cellX) {
            const CellState& cell = m_cells.at(cellIndex(cellX, cellY));
            if (cell.terrain == _game_terrain_plain
                && cell.buildingType == _game_building_none) {
                continue;
            }
            const float nearestX = qBound(static_cast<float>(cellX), x,
                                          static_cast<float>(cellX + 1));
            const float nearestY = qBound(static_cast<float>(cellY), y,
                                          static_cast<float>(cellY + 1));
            if (distanceBetween(x, y, nearestX, nearestY) < radius) {
                return true;
            }
        }
    }
    return false;
}

void DefenseGameServer::resolveZombieOverlaps()
{
    constexpr float maximumPairDistance = kGiantZombieCollisionRadius * 2.0f;
    constexpr int correctionPasses = 4;
    for (int pass = 0; pass < correctionPasses; ++pass) {
        rebuildZombieSpatialIndex();
        QSet<quint64> processedPairs;
        const QList<int> ids = m_zombies.keys();
        for (int firstId : ids) {
            auto first = m_zombies.find(firstId);
            if (first == m_zombies.end() || first->hp <= 0) {
                continue;
            }
            for (int secondId : nearbyZombieIds(first->x, first->y, maximumPairDistance)) {
                if (secondId == firstId) {
                    continue;
                }
                const int low = qMin(firstId, secondId);
                const int high = qMax(firstId, secondId);
                const quint64 pairKey = (static_cast<quint64>(static_cast<quint32>(low)) << 32)
                    | static_cast<quint32>(high);
                if (processedPairs.contains(pairKey)) {
                    continue;
                }
                processedPairs.insert(pairKey);
                auto second = m_zombies.find(secondId);
                if (second == m_zombies.end() || second->hp <= 0) {
                    continue;
                }
                float dx = second->x - first->x;
                float dy = second->y - first->y;
                float distance = qSqrt(dx * dx + dy * dy);
                const float firstRadius = zombieCollisionRadius(first->kind);
                const float secondRadius = zombieCollisionRadius(second->kind);
                const float minimumDistance = firstRadius + secondRadius;
                if (distance >= minimumDistance) {
                    continue;
                }
                float nx = 0.0f;
                float ny = 0.0f;
                if (distance <= 0.0001f) {
                    const float angle = static_cast<float>((low * 37 + high * 17) % 360)
                        * static_cast<float>(M_PI / 180.0);
                    nx = qCos(angle);
                    ny = qSin(angle);
                } else {
                    nx = dx / distance;
                    ny = dy / distance;
                }
                const float correction = (minimumDistance - distance + 0.002f) * 0.5f;
                const QPointF firstCandidate(first->x - nx * correction,
                                             first->y - ny * correction);
                const QPointF secondCandidate(second->x + nx * correction,
                                              second->y + ny * correction);
                const bool firstCanMove = !isZombiePositionBlocked(
                    firstCandidate.x(), firstCandidate.y(), firstRadius);
                const bool secondCanMove = !isZombiePositionBlocked(
                    secondCandidate.x(), secondCandidate.y(), secondRadius);
                if (firstCanMove) {
                    first->x = static_cast<float>(firstCandidate.x());
                    first->y = static_cast<float>(firstCandidate.y());
                }
                if (secondCanMove) {
                    second->x = static_cast<float>(secondCandidate.x());
                    second->y = static_cast<float>(secondCandidate.y());
                }
                if (firstCanMove && !secondCanMove) {
                    const QPointF extra(first->x - nx * correction,
                                        first->y - ny * correction);
                    if (!isZombiePositionBlocked(extra.x(), extra.y(), firstRadius)) {
                        first->x = static_cast<float>(extra.x());
                        first->y = static_cast<float>(extra.y());
                    }
                } else if (!firstCanMove && secondCanMove) {
                    const QPointF extra(second->x + nx * correction,
                                        second->y + ny * correction);
                    if (!isZombiePositionBlocked(extra.x(), extra.y(), secondRadius)) {
                        second->x = static_cast<float>(extra.x());
                        second->y = static_cast<float>(extra.y());
                    }
                }
            }
        }
    }
    rebuildZombieSpatialIndex();
}

float DefenseGameServer::distanceToNearestPlayerForce(float x, float y) const
{
    const int cellX = qBound(0, qFloor(x), GAME_MAP_WIDTH - 1);
    const int cellY = qBound(0, qFloor(y), GAME_MAP_HEIGHT - 1);
    const int index = cellIndex(cellX, cellY);
    if (index >= 0 && index < m_flowCosts.size() && qIsFinite(m_flowCosts.at(index))) {
        float distance = m_flowCosts.at(index);
        const std::uint8_t targetKind = m_flowTargetKinds.value(index, 0);
        if (targetKind == 1) {
            const qint64 targetId = m_flowTargetIds.value(index, -1);
            if (targetId >= 0 && targetId < m_cells.size()) {
                const int type = m_cells.at(static_cast<int>(targetId)).buildingType;
                distance -= (isProductionBuilding(type) || isPopulationBuilding(type))
                    ? kFlowExtractorSourceCost
                    : kFlowOffensiveBuildingSourceCost;
            }
        }
        return qMax(0.0f, distance);
    }
    return std::numeric_limits<float>::max();
}

void DefenseGameServer::rebuildFlowField(qint64 nowMs)
{
    const int totalCells = GAME_MAP_WIDTH * GAME_MAP_HEIGHT;
    const float infinity = std::numeric_limits<float>::max();
    m_flowCosts.fill(infinity, totalCells);
    m_flowNextCells.fill(QPoint(-1, -1), totalCells);
    m_flowTargetIds.fill(0, totalCells);
    m_flowTargetKinds.fill(0, totalCells);

    QSet<qint64> onlineUsers;
    for (auto player = m_players.constBegin(); player != m_players.constEnd(); ++player) {
        if (player->online) {
            onlineUsers.insert(player->userId);
        }
    }
    using OpenNode = std::pair<float, int>;
    std::priority_queue<OpenNode, std::vector<OpenNode>, std::greater<OpenNode>> open;
    QSet<int> sourceCells;
    auto addSource = [&](int index, float initialCost, std::uint8_t kind, qint64 targetId) {
        if (index < 0 || index >= totalCells || initialCost >= m_flowCosts[index]) {
            return;
        }
        m_flowCosts[index] = initialCost;
        m_flowNextCells[index] = QPoint(index % GAME_MAP_WIDTH, index / GAME_MAP_WIDTH);
        m_flowTargetKinds[index] = kind;
        m_flowTargetIds[index] = targetId;
        sourceCells.insert(index);
        open.push({initialCost, index});
    };

    for (int index = 0; index < m_cells.size(); ++index) {
        const CellState& cell = m_cells.at(index);
        if (cell.buildingType == _game_building_none
            || !onlineUsers.contains(cell.buildingOwnerId)
            || isReinforcedZoneBarrier(index % GAME_MAP_WIDTH,
                                       index / GAME_MAP_WIDTH, nowMs)) {
            continue;
        }
        if (isProductionBuilding(cell.buildingType)
            || isPopulationBuilding(cell.buildingType)) {
            addSource(index, kFlowExtractorSourceCost, 1, index);
        } else if (cell.buildingType == _game_building_turret
                   || cell.buildingType == _game_building_heavy_turret) {
            addSource(index, kFlowOffensiveBuildingSourceCost, 1, index);
        }
    }
    for (qint64 userId : onlineUsers) {
        const PlayerState* player = playerForUser(userId);
        if (!player || player->respawnAtMs > 0) {
            continue;
        }
        const int playerX = qBound(0, qFloor(player->x), GAME_MAP_WIDTH - 1);
        const int playerY = qBound(0, qFloor(player->y), GAME_MAP_HEIGHT - 1);
        if (!isReinforcedZoneBarrier(playerX, playerY, nowMs)) {
            addSource(cellIndex(playerX, playerY), kFlowPlayerSourceCost, 2, userId);
        }
    }

    static const QPoint directions[] = {
        QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)
    };
    while (!open.empty()) {
        const float queuedCost = open.top().first;
        const int current = open.top().second;
        open.pop();
        if (queuedCost > m_flowCosts.at(current)) {
            continue;
        }
        const int x = current % GAME_MAP_WIDTH;
        const int y = current / GAME_MAP_WIDTH;
        const float entryCost = sourceCells.contains(current)
            ? 1.0f : zombieTraversalCost(x, y, true, nowMs);
        if (!qIsFinite(entryCost) || entryCost == infinity) {
            continue;
        }
        for (const QPoint& direction : directions) {
            const int nx = x + direction.x();
            const int ny = y + direction.y();
            if (!inBounds(nx, ny)
                || m_cells.at(cellIndex(nx, ny)).terrain != _game_terrain_plain) {
                continue;
            }
            const int neighbor = cellIndex(nx, ny);
            const float candidate = queuedCost + entryCost;
            if (candidate >= m_flowCosts.at(neighbor)) {
                continue;
            }
            m_flowCosts[neighbor] = candidate;
            m_flowNextCells[neighbor] = QPoint(x, y);
            m_flowTargetKinds[neighbor] = m_flowTargetKinds.at(current);
            m_flowTargetIds[neighbor] = m_flowTargetIds.at(current);
            open.push({candidate, neighbor});
        }
    }
    rebuildSectorFlowField(nowMs);
    m_lastFlowFieldAtMs = nowMs;
}

int DefenseGameServer::sectorIndexForCell(int x, int y) const
{
    const int sectorX = qBound(0, x / kFlowSectorSize, kFlowSectorColumns - 1);
    const int sectorY = qBound(0, y / kFlowSectorSize, kFlowSectorRows - 1);
    return sectorY * kFlowSectorColumns + sectorX;
}

void DefenseGameServer::rebuildSectorFlowField(qint64 nowMs)
{
    const int sectorCount = kFlowSectorColumns * kFlowSectorRows;
    const float infinity = std::numeric_limits<float>::max();
    m_sectorFlowCosts.fill(infinity, sectorCount);
    m_sectorFlowNext.fill(-1, sectorCount);

    using OpenNode = std::pair<float, int>;
    std::priority_queue<OpenNode, std::vector<OpenNode>, std::greater<OpenNode>> open;
    QSet<int> sourceSectors;
    for (int index = 0; index < m_flowTargetKinds.size(); ++index) {
        if (m_flowTargetKinds.at(index) == 0
            || m_flowNextCells.value(index, QPoint(-1, -1))
                != QPoint(index % GAME_MAP_WIDTH, index / GAME_MAP_WIDTH)) {
            continue;
        }
        const int sector = sectorIndexForCell(index % GAME_MAP_WIDTH,
                                              index / GAME_MAP_WIDTH);
        if (!sourceSectors.contains(sector)) {
            sourceSectors.insert(sector);
            m_sectorFlowCosts[sector] = 0.0f;
            m_sectorFlowNext[sector] = sector;
            open.push({0.0f, sector});
        }
    }

    auto boundaryCost = [this, nowMs](int fromSector, int toSector) {
        const int fromX = fromSector % kFlowSectorColumns;
        const int fromY = fromSector / kFlowSectorColumns;
        const int toX = toSector % kFlowSectorColumns;
        const int toY = toSector / kFlowSectorColumns;
        float best = std::numeric_limits<float>::max();
        if (fromX != toX) {
            const int leftSectorX = qMin(fromX, toX);
            const int leftX = qMin(GAME_MAP_WIDTH - 1,
                                   (leftSectorX + 1) * kFlowSectorSize - 1);
            const int rightX = leftX + 1;
            const int startY = qMax(fromY, toY) * kFlowSectorSize;
            const int endY = qMin(GAME_MAP_HEIGHT, startY + kFlowSectorSize);
            if (rightX >= GAME_MAP_WIDTH) {
                return best;
            }
            for (int y = startY; y < endY; ++y) {
                best = qMin(best,
                    zombieTraversalCost(leftX, y, true, nowMs)
                    + zombieTraversalCost(rightX, y, true, nowMs));
            }
        } else {
            const int topSectorY = qMin(fromY, toY);
            const int topY = qMin(GAME_MAP_HEIGHT - 1,
                                  (topSectorY + 1) * kFlowSectorSize - 1);
            const int bottomY = topY + 1;
            const int startX = qMax(fromX, toX) * kFlowSectorSize;
            const int endX = qMin(GAME_MAP_WIDTH, startX + kFlowSectorSize);
            if (bottomY >= GAME_MAP_HEIGHT) {
                return best;
            }
            for (int x = startX; x < endX; ++x) {
                best = qMin(best,
                    zombieTraversalCost(x, topY, true, nowMs)
                    + zombieTraversalCost(x, bottomY, true, nowMs));
            }
        }
        return best;
    };

    static const QPoint directions[] = {
        QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)
    };
    while (!open.empty()) {
        const float queuedCost = open.top().first;
        const int current = open.top().second;
        open.pop();
        if (queuedCost > m_sectorFlowCosts.at(current)) {
            continue;
        }
        const int sectorX = current % kFlowSectorColumns;
        const int sectorY = current / kFlowSectorColumns;
        for (const QPoint& direction : directions) {
            const int neighborX = sectorX + direction.x();
            const int neighborY = sectorY + direction.y();
            if (neighborX < 0 || neighborX >= kFlowSectorColumns
                || neighborY < 0 || neighborY >= kFlowSectorRows) {
                continue;
            }
            const int neighbor = neighborY * kFlowSectorColumns + neighborX;
            const float crossingCost = boundaryCost(neighbor, current);
            if (!qIsFinite(crossingCost) || crossingCost == infinity) {
                continue;
            }
            const float candidate = queuedCost + qMax(1.0f, crossingCost);
            if (candidate >= m_sectorFlowCosts.at(neighbor)) {
                continue;
            }
            m_sectorFlowCosts[neighbor] = candidate;
            m_sectorFlowNext[neighbor] = current;
            open.push({candidate, neighbor});
        }
    }
}

void DefenseGameServer::moveZombieWithFlowField(ZombieState& zombie, qint64 nowMs,
                                                 float deltaSeconds)
{
    float remaining = m_zombieSpeedCellsPerSecond
        * zombieSpeedMultiplier(zombie.kind) * deltaSeconds;
    if (zombie.lastProgressAtMs > 0
        && nowMs - zombie.lastProgressAtMs >= kZombieBlockedAttackDelayMs) {
        m_lastFlowFieldAtMs = 0;
    }
    while (remaining > 0.0001f) {
        const int x = qBound(0, qFloor(zombie.x), GAME_MAP_WIDTH - 1);
        const int y = qBound(0, qFloor(zombie.y), GAME_MAP_HEIGHT - 1);
        const int index = cellIndex(x, y);
        if (index >= m_flowNextCells.size() || m_flowTargetKinds.at(index) == 0) {
            return;
        }

        const std::uint8_t targetKind = m_flowTargetKinds.at(index);
        const qint64 targetId = m_flowTargetIds.at(index);
        zombie.targetBuildingIndex = targetKind == 1 ? static_cast<int>(targetId) : -1;
        zombie.targetUserId = targetKind == 2 ? targetId
            : targetKind == 1 && targetId >= 0 && targetId < m_cells.size()
                ? m_cells.at(static_cast<int>(targetId)).buildingOwnerId : 0;

        if (targetKind == 1 && targetId >= 0 && targetId < m_cells.size()) {
            const int buildingIndex = static_cast<int>(targetId);
            const CellState& building = m_cells.at(buildingIndex);
            const QPoint buildingCell(buildingIndex % GAME_MAP_WIDTH,
                                      buildingIndex / GAME_MAP_WIDTH);
            if (isReinforcedZoneBarrier(buildingCell.x(), buildingCell.y(), nowMs)) {
                m_lastFlowFieldAtMs = 0;
                return;
            }
            if (building.buildingType != _game_building_none
                && distanceToCell(zombie.x, zombie.y, buildingCell) <= zombie.attackRange) {
                zombie.lastProgressAtMs = nowMs;
                if (nowMs - zombie.lastAttackAtMs >= kCombatIntervalMs) {
                    zombie.lastAttackAtMs = nowMs;
                    damageBuilding(zombie, buildingCell.x(), buildingCell.y(),
                                   zombieAttack(zombie.kind), nowMs);
                }
                return;
            }
        } else if (targetKind == 2) {
            PlayerState* player = playerForUser(targetId);
            if (player && player->online && player->respawnAtMs <= 0
                && distanceBetween(zombie.x, zombie.y, player->x, player->y)
                    <= zombie.attackRange) {
                zombie.lastProgressAtMs = nowMs;
                if (nowMs - zombie.lastAttackAtMs >= kCombatIntervalMs) {
                    zombie.lastAttackAtMs = nowMs;
                    damagePlayer(*player, zombieAttack(zombie.kind), nowMs);
                }
                return;
            }
        }

        const QPoint nextCell = m_flowNextCells.at(index);
        if (!inBounds(nextCell.x(), nextCell.y()) || nextCell == QPoint(x, y)) {
            return;
        }
        const CellState& next = m_cells.at(cellIndex(nextCell.x(), nextCell.y()));
        if (isReinforcedZoneBarrier(nextCell.x(), nextCell.y(), nowMs)) {
            m_lastFlowFieldAtMs = 0;
            return;
        }
        if (next.buildingType != _game_building_none
            && distanceToCell(zombie.x, zombie.y, nextCell) <= zombie.attackRange) {
            zombie.lastProgressAtMs = nowMs;
            if (nowMs - zombie.lastAttackAtMs >= kCombatIntervalMs) {
                zombie.lastAttackAtMs = nowMs;
                damageBuilding(zombie, nextCell.x(), nextCell.y(),
                               zombieAttack(zombie.kind), nowMs);
            }
            return;
        }
        if (next.terrain != _game_terrain_plain) {
            m_lastFlowFieldAtMs = 0;
            return;
        }

        const QPointF waypoint(nextCell.x() + 0.5f, nextCell.y() + 0.5f);
        const float waypointDistance = distanceBetween(zombie.x, zombie.y,
                                                       waypoint.x(), waypoint.y());
        if (waypointDistance <= 0.001f) {
            zombie.x = static_cast<float>(waypoint.x());
            zombie.y = static_cast<float>(waypoint.y());
            continue;
        }
        const QPointF direction = normalizedVector(waypoint.x() - zombie.x,
                                                    waypoint.y() - zombie.y);
        const float step = qMin(qMin(remaining, 0.12f), waypointDistance);
        const float nextX = zombie.x + static_cast<float>(direction.x()) * step;
        const float nextY = zombie.y + static_cast<float>(direction.y()) * step;
        if (next.buildingType != _game_building_none
            || next.terrain != _game_terrain_plain) {
            return;
        }
        zombie.x = nextX;
        zombie.y = nextY;
        zombie.lastProgressAtMs = nowMs;
        zombie.lastProgressX = zombie.x;
        zombie.lastProgressY = zombie.y;
        remaining -= step;
        breachZoneCell(qFloor(zombie.x), qFloor(zombie.y));
    }
}

void DefenseGameServer::moveZombie(ZombieState& zombie, qint64 nowMs, float deltaSeconds,
                                   bool mayRebuildPath)
{
    if (zombiePathNeedsUpdate(zombie, nowMs)) {
        if (!mayRebuildPath || !rebuildZombiePath(zombie, nowMs)) {
            return;
        }
    }

    PlayerState* targetPlayer = nullptr;
    CellState* targetBuilding = nullptr;
    if (zombie.targetBuildingIndex >= 0 && zombie.targetBuildingIndex < m_cells.size()) {
        CellState& cell = m_cells[zombie.targetBuildingIndex];
        const int x = zombie.targetBuildingIndex % GAME_MAP_WIDTH;
        const int y = zombie.targetBuildingIndex / GAME_MAP_WIDTH;
        if (cell.buildingType != _game_building_none && cell.buildingHp > 0
            && !isReinforcedZoneBarrier(x, y, nowMs)) {
            targetBuilding = &cell;
        }
    } else if (zombie.targetUserId > 0) {
        targetPlayer = playerForUser(zombie.targetUserId);
        if (targetPlayer
            && isReinforcedZoneBarrier(qFloor(targetPlayer->x),
                                       qFloor(targetPlayer->y), nowMs)) {
            targetPlayer = nullptr;
        }
    }

    if (zombie.targetBuildingIndex >= 0 && !targetBuilding) {
        zombie.pathUpdatedAtMs = 0;
        return;
    }
    if (zombie.targetBuildingIndex < 0 && zombie.targetUserId > 0
        && (!targetPlayer || !targetPlayer->online || targetPlayer->respawnAtMs > 0)) {
        zombie.pathUpdatedAtMs = 0;
        return;
    }
    if (!targetBuilding && !targetPlayer) {
        return;
    }

    if (targetBuilding) {
        const QPoint buildingCell(zombie.targetBuildingIndex % GAME_MAP_WIDTH,
                                  zombie.targetBuildingIndex / GAME_MAP_WIDTH);
        if (distanceToCell(zombie.x, zombie.y, buildingCell) <= zombie.attackRange) {
            zombie.lastProgressAtMs = nowMs;
            zombie.lastProgressX = zombie.x;
            zombie.lastProgressY = zombie.y;
            if (nowMs - zombie.lastAttackAtMs >= kCombatIntervalMs) {
                zombie.lastAttackAtMs = nowMs;
                damageBuilding(zombie, buildingCell.x(), buildingCell.y(),
                               zombieAttack(zombie.kind), nowMs);
            }
            return;
        }
    } else if (distanceBetween(zombie.x, zombie.y, targetPlayer->x, targetPlayer->y)
               <= zombie.attackRange) {
        if (nowMs - zombie.lastAttackAtMs >= kCombatIntervalMs) {
            zombie.lastAttackAtMs = nowMs;
            damagePlayer(*targetPlayer, zombieAttack(zombie.kind), nowMs);
        }
        return;
    }

    while (zombie.pathIndex < zombie.path.size()) {
        const QPoint cell = zombie.path.at(zombie.pathIndex);
        const QPointF waypoint(cell.x() + 0.5f, cell.y() + 0.5f);
        if (distanceBetween(zombie.x, zombie.y, waypoint.x(), waypoint.y()) > 0.04f) {
            break;
        }
        ++zombie.pathIndex;
    }

    QPoint plannedCell(-1, -1);
    QPointF waypoint = targetBuilding
        ? QPointF(zombie.targetCell.x() + 0.5f, zombie.targetCell.y() + 0.5f)
        : QPointF(targetPlayer->x, targetPlayer->y);
    if (zombie.pathIndex < zombie.path.size()) {
        plannedCell = zombie.path.at(zombie.pathIndex);
        waypoint = QPointF(plannedCell.x() + 0.5f, plannedCell.y() + 0.5f);
    }

    const int plannedIndex = plannedCell.x() >= 0
        ? cellIndex(plannedCell.x(), plannedCell.y()) : -1;
    if (plannedIndex >= 0 && zombie.plannedBreaches.contains(plannedIndex)) {
        CellState& plannedBuilding = m_cells[plannedIndex];
        if (plannedBuilding.buildingType == _game_building_none) {
            zombie.plannedBreaches.remove(plannedIndex);
        } else if (distanceToCell(zombie.x, zombie.y, plannedCell)
                   <= zombie.attackRange) {
            zombie.lastProgressAtMs = nowMs;
            zombie.lastProgressX = zombie.x;
            zombie.lastProgressY = zombie.y;
            if (nowMs - zombie.lastAttackAtMs >= kCombatIntervalMs) {
                zombie.lastAttackAtMs = nowMs;
                damageBuilding(zombie, plannedCell.x(), plannedCell.y(),
                               zombieAttack(zombie.kind), nowMs);
            }
            return;
        }
    }

    const QPointF pathDirection = normalizedVector(waypoint.x() - zombie.x,
                                                    waypoint.y() - zombie.y);

    const float step = m_zombieSpeedCellsPerSecond
        * zombieSpeedMultiplier(zombie.kind) * deltaSeconds;
    const float collisionRadius = zombieCollisionRadius(zombie.kind);
    auto blockingCellAt = [this, collisionRadius](float xPosition, float yPosition) {
        const int minX = qMax(0, qFloor(xPosition - collisionRadius));
        const int maxX = qMin(GAME_MAP_WIDTH - 1, qFloor(xPosition + collisionRadius));
        const int minY = qMax(0, qFloor(yPosition - collisionRadius));
        const int maxY = qMin(GAME_MAP_HEIGHT - 1, qFloor(yPosition + collisionRadius));
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                const CellState& cell = m_cells.at(cellIndex(x, y));
                if (cell.buildingType == _game_building_none && cell.terrain == 0) {
                    continue;
                }
                const float nearestX = qBound(static_cast<float>(x), xPosition,
                                              static_cast<float>(x + 1));
                const float nearestY = qBound(static_cast<float>(y), yPosition,
                                              static_cast<float>(y + 1));
                if (distanceBetween(xPosition, yPosition, nearestX, nearestY)
                    < collisionRadius) {
                    return QPoint(x, y);
                }
            }
        }
        return QPoint(-1, -1);
    };

    const float nextX = zombie.x + static_cast<float>(pathDirection.x()) * step;
    const float nextY = zombie.y + static_cast<float>(pathDirection.y()) * step;
    QPoint blockingCell = blockingCellAt(nextX, nextY);

    if (blockingCell.x() >= 0) {
        // A collision that is not the exact next planned breach means the world or
        // corridor changed. Replan instead of attacking an unrelated side wall.
        zombie.path.clear();
        zombie.pathIndex = 0;
        zombie.pathUpdatedAtMs = 0;
        return;
    }

    zombie.x = nextX;
    zombie.y = nextY;
    if (zombie.lastProgressAtMs <= 0
        || distanceBetween(zombie.x, zombie.y,
                           zombie.lastProgressX, zombie.lastProgressY) >= 0.08f) {
        zombie.lastProgressAtMs = nowMs;
        zombie.lastProgressX = zombie.x;
        zombie.lastProgressY = zombie.y;
    }
    const int cellX = qFloor(zombie.x);
    const int cellY = qFloor(zombie.y);
    if (inBounds(cellX, cellY)) {
        breachZoneCell(cellX, cellY);
    }
}

bool DefenseGameServer::zombiePathNeedsUpdate(const ZombieState& zombie, qint64 nowMs) const
{
    if (zombie.targetBuildingIndex < 0 && zombie.targetUserId <= 0) {
        return zombie.pathUpdatedAtMs <= 0
            || nowMs - zombie.pathUpdatedAtMs >= kZombiePathRefreshMs;
    }
    if (nowMs - zombie.pathUpdatedAtMs >= kZombiePathRefreshMs) {
        return true;
    }
    if (zombie.targetBuildingIndex >= 0) {
        if (zombie.targetBuildingIndex >= m_cells.size()) {
            return true;
        }
        const CellState& target = m_cells.at(zombie.targetBuildingIndex);
        if (target.buildingType == _game_building_none || target.buildingHp <= 0) {
            return true;
        }
    } else {
        const PlayerState* target = playerForUser(zombie.targetUserId);
        if (!target || !target->online || target->respawnAtMs > 0) {
            return true;
        }
        const QPoint currentTargetCell(qFloor(target->x), qFloor(target->y));
        const int targetShift = qAbs(currentTargetCell.x() - zombie.targetCell.x())
            + qAbs(currentTargetCell.y() - zombie.targetCell.y());
        if (targetShift >= 2) {
            return true;
        }
    }
    if (zombie.lastProgressAtMs > 0
        && nowMs - zombie.lastProgressAtMs >= kZombieStuckTimeoutMs
        && distanceBetween(zombie.x, zombie.y,
                           zombie.lastProgressX, zombie.lastProgressY) < 0.05f) {
        return true;
    }
    const int inspectEnd = qMin(zombie.path.size(), zombie.pathIndex + 3);
    for (int i = zombie.pathIndex; i < inspectEnd; ++i) {
        const QPoint next = zombie.path.at(i);
        if (!inBounds(next.x(), next.y())) {
            return true;
        }
        const CellState& cell = m_cells.at(cellIndex(next.x(), next.y()));
        const int nextIndex = cellIndex(next.x(), next.y());
        if (cell.terrain != 0 || (cell.buildingType != _game_building_none
                                 && !zombie.plannedBreaches.contains(nextIndex))) {
            return true;
        }
    }
    return false;
}

bool DefenseGameServer::rebuildZombiePath(ZombieState& zombie, qint64 nowMs)
{
    zombie.path.clear();
    zombie.pathIndex = 0;
    zombie.targetUserId = 0;
    zombie.targetBuildingIndex = -1;
    zombie.targetCell = QPoint(-1, -1);
    zombie.pathUsesBreaches = false;
    zombie.plannedBreaches.clear();
    zombie.plannedPathCost = 0.0f;
    zombie.pathUpdatedAtMs = nowMs;

    const int rawStartX = qFloor(zombie.x);
    const int rawStartY = qFloor(zombie.y);
    const QPoint start(qBound(0, rawStartX, GAME_MAP_WIDTH - 1),
                       qBound(0, rawStartY, GAME_MAP_HEIGHT - 1));
    QHash<int, qint64> playerGoals;
    QHash<int, qint64> shieldGoals;
    QHash<int, qint64> extractorGoals;
    QHash<int, qint64> offensiveBuildingGoals;
    const float searchRangeSquared = zombie.targetSearchRange * zombie.targetSearchRange;
    const int minX = qMax(0, qFloor(zombie.x - zombie.targetSearchRange));
    const int maxX = qMin(GAME_MAP_WIDTH - 1, qFloor(zombie.x + zombie.targetSearchRange));
    const int minY = qMax(0, qFloor(zombie.y - zombie.targetSearchRange));
    const int maxY = qMin(GAME_MAP_HEIGHT - 1, qFloor(zombie.y + zombie.targetSearchRange));
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const CellState& cell = m_cells.at(cellIndex(x, y));
            if (cell.buildingType == _game_building_none || cell.buildingOwnerId <= 0
                || cell.buildingHp <= 0) {
                continue;
            }
            const float dx = x + 0.5f - zombie.x;
            const float dy = y + 0.5f - zombie.y;
            if (dx * dx + dy * dy > searchRangeSquared) {
                continue;
            }
            if (cell.buildingType == _game_building_wave_shield
                && distanceBetween(zombie.x, zombie.y, x + 0.5f, y + 0.5f)
                    <= GAME_WAVE_SHIELD_RADIUS) {
                shieldGoals.insert(cellIndex(x, y), cell.buildingOwnerId);
            } else if (isProductionBuilding(cell.buildingType)
                || isPopulationBuilding(cell.buildingType)) {
                extractorGoals.insert(cellIndex(x, y), cell.buildingOwnerId);
            } else if (cell.buildingType == _game_building_turret
                       || cell.buildingType == _game_building_heavy_turret) {
                offensiveBuildingGoals.insert(cellIndex(x, y), cell.buildingOwnerId);
            }
        }
    }
    for (auto player = m_players.constBegin(); player != m_players.constEnd(); ++player) {
        if (!player->online || player->respawnAtMs > 0) {
            continue;
        }
        const float dx = player->x - zombie.x;
        const float dy = player->y - zombie.y;
        if (dx * dx + dy * dy > searchRangeSquared) {
            continue;
        }
        const int x = qBound(0, qFloor(player->x), GAME_MAP_WIDTH - 1);
        const int y = qBound(0, qFloor(player->y), GAME_MAP_HEIGHT - 1);
        playerGoals.insert(cellIndex(x, y), player->userId);
    }

    ZombiePathResult result;
    bool targetsBuilding = false;
    const QHash<int, qint64>* targetGroups[] = {
        &shieldGoals, &playerGoals, &extractorGoals, &offensiveBuildingGoals
    };
    for (int groupIndex = 0; groupIndex < 4 && result.cells.isEmpty(); ++groupIndex) {
        const QHash<int, qint64>& goals = *targetGroups[groupIndex];
        if (goals.isEmpty()) {
            continue;
        }
        const ZombiePathResult openPath = findZombiePath(start, goals, false);
        const ZombiePathResult breachPath = findZombiePath(start, goals, true);
        result = openPath;
        const bool openPathUnavailable = openPath.cells.isEmpty();
        const bool breachIsStrategicallyBetter = !breachPath.cells.isEmpty()
            && breachPath.usesBreaches
            && breachPath.cost + kZombieBreachPathMinimumSavings < openPath.cost
            && breachPath.cost < openPath.cost * kZombieBreachPathSavingsRatio;
        if (openPathUnavailable || breachIsStrategicallyBetter) {
            result = breachPath;
        }
        targetsBuilding = groupIndex != 1 && !result.cells.isEmpty();
    }
    if (result.cells.isEmpty()) {
        return false;
    }

    zombie.path = std::move(result.cells);
    zombie.pathIndex = inBounds(rawStartX, rawStartY) ? 1 : 0;
    zombie.targetUserId = result.targetUserId;
    if (targetsBuilding) {
        const QPoint target = zombie.path.last();
        zombie.targetBuildingIndex = cellIndex(target.x(), target.y());
        zombie.targetCell = target;
    } else if (const PlayerState* target = playerForUser(result.targetUserId)) {
        zombie.targetCell = QPoint(qFloor(target->x), qFloor(target->y));
    }
    zombie.pathUsesBreaches = result.usesBreaches;
    zombie.plannedBreaches = std::move(result.breachCells);
    zombie.plannedPathCost = result.cost;
    zombie.lastProgressAtMs = nowMs;
    zombie.lastProgressX = zombie.x;
    zombie.lastProgressY = zombie.y;
    return true;
}

DefenseGameServer::ZombiePathResult DefenseGameServer::findZombiePath(
    const QPoint& start, const QHash<int, qint64>& goals, bool allowBreaching) const
{
    ZombiePathResult result;
    if (!inBounds(start.x(), start.y()) || goals.isEmpty()) {
        return result;
    }

    const int totalCells = GAME_MAP_WIDTH * GAME_MAP_HEIGHT;
    const int startIndex = cellIndex(start.x(), start.y());
    const float infinity = std::numeric_limits<float>::max();
    QVector<float> costs(totalCells, infinity);
    QVector<int> previous(totalCells, -1);
    QVector<bool> closed(totalCells, false);
    using OpenNode = std::pair<float, int>;
    std::priority_queue<OpenNode, std::vector<OpenNode>, std::greater<OpenNode>> open;

    auto heuristic = [&goals](int x, int y) {
        int best = GAME_MAP_WIDTH + GAME_MAP_HEIGHT;
        for (auto goal = goals.constBegin(); goal != goals.constEnd(); ++goal) {
            const int goalX = goal.key() % GAME_MAP_WIDTH;
            const int goalY = goal.key() / GAME_MAP_WIDTH;
            best = qMin(best, qAbs(goalX - x) + qAbs(goalY - y));
        }
        return static_cast<float>(best);
    };

    costs[startIndex] = 0.0f;
    open.push({heuristic(start.x(), start.y()), startIndex});
    static const QPoint directions[] = {
        QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)
    };

    int reachedGoal = -1;
    const qint64 pathNowMs = QDateTime::currentMSecsSinceEpoch();
    while (!open.empty()) {
        const int current = open.top().second;
        open.pop();
        if (closed[current]) {
            continue;
        }
        closed[current] = true;
        if (goals.contains(current)) {
            reachedGoal = current;
            break;
        }

        const int x = current % GAME_MAP_WIDTH;
        const int y = current / GAME_MAP_WIDTH;
        for (const QPoint& direction : directions) {
            const int nx = x + direction.x();
            const int ny = y + direction.y();
            if (!inBounds(nx, ny)) {
                continue;
            }
            const int next = cellIndex(nx, ny);
            if (closed[next]) {
                continue;
            }
            const float traversalCost = zombieTraversalCost(nx, ny, allowBreaching, pathNowMs);
            if (traversalCost >= infinity) {
                continue;
            }
            const float candidate = costs[current] + traversalCost;
            if (candidate >= costs[next]) {
                continue;
            }
            costs[next] = candidate;
            previous[next] = current;
            open.push({candidate + heuristic(nx, ny), next});
        }
    }

    if (reachedGoal < 0) {
        return result;
    }
    for (int current = reachedGoal; current >= 0; current = previous[current]) {
        result.cells.prepend(QPoint(current % GAME_MAP_WIDTH, current / GAME_MAP_WIDTH));
        if (current == startIndex) {
            break;
        }
    }
    if (result.cells.isEmpty() || result.cells.first() != start) {
        result.cells.clear();
        return result;
    }
    result.targetUserId = goals.value(reachedGoal);
    result.cost = costs[reachedGoal];
    result.usesBreaches = false;
    for (const QPoint& cell : result.cells) {
        const int index = cellIndex(cell.x(), cell.y());
        if (m_cells.at(index).buildingType != _game_building_none) {
            result.usesBreaches = true;
            result.breachCells.insert(index);
        }
    }
    return result;
}

float DefenseGameServer::zombieTraversalCost(int x, int y, bool allowBreaching,
                                             qint64 nowMs) const
{
    if (!inBounds(x, y)) {
        return std::numeric_limits<float>::max();
    }
    if (nowMs <= 0) {
        nowMs = QDateTime::currentMSecsSinceEpoch();
    }
    if (isReinforcedZoneBarrier(x, y, nowMs)) {
        return std::numeric_limits<float>::max();
    }
    const CellState& cell = m_cells.at(cellIndex(x, y));
    if (cell.terrain != 0) {
        return std::numeric_limits<float>::max();
    }
    if (cell.buildingType == _game_building_none) {
        return 1.0f;
    }
    if (!allowBreaching) {
        return std::numeric_limits<float>::max();
    }
    const float hitsToDestroy = static_cast<float>(qMax(1, cell.buildingHp))
        / qMax(1, GAME_ZOMBIE_BASE_ATTACK);
    float destructionWeight = 1.0f;
    switch (cell.buildingType) {
    case _game_building_wall: destructionWeight = 1.35f; break;
    case _game_building_door: destructionWeight = 1.1f; break;
    case _game_building_turret: destructionWeight = 0.65f; break;
    case _game_building_heavy_turret: destructionWeight = 0.5f; break;
    default:
        destructionWeight = (isProductionBuilding(cell.buildingType)
                             || isPopulationBuilding(cell.buildingType)) ? 0.8f : 1.0f;
        break;
    }
    float shieldCost = 0.0f;
    if (const ZoneState* zone = zoneById(cell.zoneId)) {
        shieldCost = zone->state == _game_zone_permanent ? zone->shieldLayers * 18.0f : 0.0f;
    }
    // 破坏高价值攻击建筑本身有战略收益，因此权重更低；永久区护盾则显著抬高代价。
    return 1.0f + kZombieBreachBaseCost + hitsToDestroy * destructionWeight + shieldCost;
}

void DefenseGameServer::damageBuilding(ZombieState&, int x, int y, int damage, qint64 nowMs)
{
    CellState& cell = m_cells[cellIndex(x, y)];
    const ZoneState* protectedZone = protectingZoneForCell(x, y);
    ZoneState* zone = protectedZone ? zoneById(protectedZone->id) : nullptr;
    if (zone && zone->state == _game_zone_permanent && nowMs < zone->reinforceUntilMs) {
        return;
    }
    if (zone && zone->state == _game_zone_permanent && zone->shieldLayers > 0) {
        if (cell.buildingHp - damage <= 0) {
            --zone->shieldLayers;
            zone->reinforceUntilMs = nowMs + kReinforcementMs;
            cell.buildingHp = buildingMaxHp(cell.buildingType, qMax(1, cell.buildingLevel));
            m_lastFlowFieldAtMs = 0;
            sendEvent(zone->ownerId, _game_event_warning,
                      QStringLiteral("永久区损失一层护盾，进入 12 小时增强期，剩余 %1 层。")
                          .arg(zone->shieldLayers));
            return;
        }
    }

    cell.buildingHp -= damage;
    if (cell.buildingHp <= 0) {
        m_turretLastAttackAtMs.remove(cellIndex(x, y));
        m_repairs.remove(cellIndex(x, y));
        cell.buildingType = _game_building_none;
        cell.buildingOwnerId = 0;
        cell.buildingHp = 0;
        cell.buildingLevel = 0;
        cell.buildingExp = 0;
        m_lastFlowFieldAtMs = 0;
    }
}

void DefenseGameServer::breachZoneCell(int x, int y)
{
    CellState& cell = m_cells[cellIndex(x, y)];
    ZoneState* zone = zoneById(cell.zoneId);
    if (!zone) {
        return;
    }
    if (zone->state == _game_zone_permanent
        && (zone->shieldLayers > 0 || QDateTime::currentMSecsSinceEpoch() < zone->reinforceUntilMs)) {
        return;
    }

    const int index = cellIndex(x, y);
    zone->cells.remove(index);
    cell.zoneId = 0;
    if (zone->cells.isEmpty()) {
        const qint64 ownerId = zone->ownerId;
        const int zoneId = zone->id;
        m_zones.remove(zoneId);
        if (PlayerState* owner = playerForUser(ownerId)) {
            refreshPlayerRespawnLocation(*owner);
        }
        sendEvent(ownerId, _game_event_warning, QStringLiteral("安全区已经完全沦陷。"));
    }
}

void DefenseGameServer::damagePlayer(PlayerState& player, int damage, qint64 nowMs)
{
    if (player.respawnAtMs > 0 || nowMs < player.invulnerableUntilMs) {
        return;
    }
    player.hp -= damage;
    if (player.hp > 0) {
        return;
    }
    player.hp = 0;
    refreshPlayerRespawnLocation(player);
    player.invulnerableUntilMs = 0;
    player.respawnAtMs = nowMs + kPlayerRespawnDelayMs;
    player.moveX = 0.0f;
    player.moveY = 0.0f;
    player.hasMoveTarget = false;
    removePlayerAssets(player.userId);
    sendEvent(player.userId, _game_event_warning,
              player.respawnZoneId > 0
                  ? QStringLiteral("角色已倒下，安全区及区内建筑将保留，10 秒后在复活地盘重生。")
                  : QStringLiteral("角色已倒下，安全区及区内建筑将保留，10 秒后在初始位置重生。"));
}

void DefenseGameServer::removeDeadZombies()
{
    QList<int> dead;
    for (auto it = m_zombies.constBegin(); it != m_zombies.constEnd(); ++it) {
        if (it->hp <= 0) {
            dead.append(it.key());
        }
    }
    for (int id : dead) {
        auto zombie = m_zombies.find(id);
        if (zombie != m_zombies.end()) {
            recycleZombieState(std::move(zombie.value()));
            m_zombies.erase(zombie);
        }
    }
}

void DefenseGameServer::resetWaveIfNoLivingPlayers(qint64 nowMs)
{
    bool hasOnlinePlayer = false;
    bool hasLivingPlayer = false;
    for (auto player = m_players.constBegin(); player != m_players.constEnd(); ++player) {
        if (!player->online) {
            continue;
        }
        hasOnlinePlayer = true;
        if (player->respawnAtMs <= 0 && player->hp > 0) {
            hasLivingPlayer = true;
            break;
        }
    }

    if (!hasOnlinePlayer) {
        return;
    }
    if (hasLivingPlayer) {
        m_waveResetForNoSurvivors = false;
        return;
    }
    if (!m_waveResetForNoSurvivors) {
        resetWaveProgress(nowMs);
    }
}

void DefenseGameServer::resetWaveProgress(qint64 nowMs)
{
    auto recycleAll = [this](QHash<int, ZombieState>& zombies) {
        for (auto zombie = zombies.begin(); zombie != zombies.end();) {
            ZombieState recycled = std::move(zombie.value());
            zombie = zombies.erase(zombie);
            recycleZombieState(std::move(recycled));
        }
    };
    recycleAll(m_zombies);
    recycleAll(m_suspendedZombies);

    initializeDormantHordes();
    for (QVector<int>& bucket : m_zombieSpatialBuckets) {
        bucket.clear();
    }
    m_flowCosts.clear();
    m_flowNextCells.clear();
    m_flowTargetIds.clear();
    m_flowTargetKinds.clear();
    m_sectorFlowCosts.clear();
    m_sectorFlowNext.clear();
    m_wave = 0;
    m_waveAssaultUntilMs = 0;
    m_nextWaveAtMs = nowMs + kFirstWaveDelayMs;
    m_lastFlowFieldAtMs = 0;
    m_lastDormantReplenishAtMs = nowMs;
    m_lastDormantAdvanceAtMs = nowMs;
    m_lastDormantMaterializeAtMs = nowMs;
    m_aiSchedulerCursor = 0;
    m_waveResetForNoSurvivors = true;
    sendEvent(0, _game_event_wave,
              QStringLiteral("当前没有存活玩家，波次与僵尸强度已重置。下一波将在 45 秒后开始。"));
}

void DefenseGameServer::scheduleNextDisaster(qint64 nowMs)
{
    const int intervalRange = qMax(0,
        GAME_DISASTER_MAX_INTERVAL_SECONDS - GAME_DISASTER_MIN_INTERVAL_SECONDS);
    const int intervalSeconds = GAME_DISASTER_MIN_INTERVAL_SECONDS
        + (intervalRange > 0
               ? QRandomGenerator::global()->bounded(intervalRange + 1) : 0);
    m_nextDisasterAtMs = nowMs + intervalSeconds * 1000LL;
}

void DefenseGameServer::processRandomDisasters(qint64 nowMs)
{
    QVector<const PlayerState*> livingPlayers;
    for (auto player = m_players.constBegin(); player != m_players.constEnd(); ++player) {
        if (player->online && player->respawnAtMs <= 0 && player->hp > 0) {
            livingPlayers.append(&player.value());
        }
    }
    if (livingPlayers.isEmpty()) {
        m_nextDisasterAtMs = 0;
        return;
    }
    if (m_nextDisasterAtMs <= 0) {
        scheduleNextDisaster(nowMs);
        return;
    }
    if (nowMs < m_nextDisasterAtMs) {
        return;
    }

    if (QRandomGenerator::global()->bounded(100) < 55) {
        const PlayerState* target = livingPlayers.at(
            QRandomGenerator::global()->bounded(livingPlayers.size()));
        const double angle = QRandomGenerator::global()->generateDouble() * 2.0 * M_PI;
        const double distance = 8.0 + QRandomGenerator::global()->generateDouble() * 6.0;
        const QPointF center(
            qBound(0.5, target->x + qCos(angle) * distance, GAME_MAP_WIDTH - 0.5),
            qBound(0.5, target->y + qSin(angle) * distance, GAME_MAP_HEIGHT - 0.5));
        const int requested = qMin(GAME_DISASTER_OUTBREAK_MAX_ZOMBIES,
            GAME_DISASTER_OUTBREAK_BASE_ZOMBIES
                + qMax(0, m_wave) * GAME_DISASTER_OUTBREAK_ZOMBIES_PER_WAVE);
        const int spawned = triggerZombieOutbreak(center, requested);
        if (spawned > 0) {
            m_waveAssaultUntilMs = qMax(m_waveAssaultUntilMs, nowMs + 30000LL);
            m_lastFlowFieldAtMs = 0;
            sendEvent(0, _game_event_warning,
                      QStringLiteral("灾难事件：坐标 (%1, %2) 附近突然爆发尸潮，%3 只僵尸集中出现。")
                          .arg(qFloor(center.x())).arg(qFloor(center.y())).arg(spawned));
        }
    } else {
        triggerLightningStrike(randomLightningStrikePoint(), nowMs);
    }
    scheduleNextDisaster(nowMs);
}

int DefenseGameServer::triggerZombieOutbreak(const QPointF& center, int zombieCount)
{
    if (zombieCount <= 0 || m_zombies.size() >= kMaxActiveZombies) {
        return 0;
    }
    const QVector<bool> allowed = zombieSpawnAllowedMask();
    QVector<QPair<float, int>> rankedCandidates;
    rankedCandidates.reserve(allowed.size());
    const int centerX = qBound(0, qFloor(center.x()), GAME_MAP_WIDTH - 1);
    const int centerY = qBound(0, qFloor(center.y()), GAME_MAP_HEIGHT - 1);
    for (int y = 0; y < GAME_MAP_HEIGHT; ++y) {
        for (int x = 0; x < GAME_MAP_WIDTH; ++x) {
            const int index = cellIndex(x, y);
            if (!allowed.value(index, false)) {
                continue;
            }
            const int dx = x - centerX;
            const int dy = y - centerY;
            rankedCandidates.append({static_cast<float>(dx * dx + dy * dy), index});
        }
    }
    std::sort(rankedCandidates.begin(), rankedCandidates.end(),
              [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    const int candidatePoolSize = qMin(rankedCandidates.size(),
        qMax(zombieCount, zombieCount * 2));
    QVector<int> candidates;
    candidates.reserve(candidatePoolSize);
    for (int i = 0; i < candidatePoolSize; ++i) {
        candidates.append(rankedCandidates.at(i).second);
    }

    const int limit = qMin(zombieCount, candidates.size());
    int spawned = 0;
    for (int i = 0; i < limit && m_zombies.size() < kMaxActiveZombies; ++i) {
        const int selected = QRandomGenerator::global()->bounded(i, candidates.size());
        qSwap(candidates[i], candidates[selected]);
        const int index = candidates.at(i);
        const int kind = m_wave >= 4 && i % 15 == 14
            ? GAME_ZOMBIE_KIND_GIANT
            : m_wave >= 2 && i % 5 == 4
                ? GAME_ZOMBIE_KIND_ELITE : GAME_ZOMBIE_KIND_NORMAL;
        const int before = m_zombies.size();
        spawnZombieAt(QPointF(index % GAME_MAP_WIDTH + 0.5f,
                              index / GAME_MAP_WIDTH + 0.5f),
                      45 + qMax(1, m_wave) * 8, kind);
        if (m_zombies.size() > before) {
            ++spawned;
            if (index < m_dormantZombieDensity.size()
                && m_dormantZombieDensity[index] > 0) {
                --m_dormantZombieDensity[index];
            }
        }
    }
    rebuildZombieSpatialIndex();
    return spawned;
}

QPointF DefenseGameServer::randomLightningStrikePoint() const
{
    QVector<const PlayerState*> livingPlayers;
    QSet<qint64> onlineUsers;
    for (auto player = m_players.constBegin(); player != m_players.constEnd(); ++player) {
        if (player->online && player->respawnAtMs <= 0 && player->hp > 0) {
            livingPlayers.append(&player.value());
            onlineUsers.insert(player->userId);
        }
    }
    if (livingPlayers.isEmpty()) {
        return QPointF(GAME_MAP_WIDTH / 2.0, GAME_MAP_HEIGHT / 2.0);
    }

    if (QRandomGenerator::global()->bounded(100)
        < GAME_DISASTER_LIGHTNING_NEAR_TERRITORY_PERCENT) {
        QVector<int> ownedTerritoryCells;
        for (auto zone = m_zones.constBegin(); zone != m_zones.constEnd(); ++zone) {
            if (!onlineUsers.contains(zone->ownerId)) {
                continue;
            }
            ownedTerritoryCells.reserve(ownedTerritoryCells.size() + zone->cells.size());
            for (int index : zone->cells) {
                ownedTerritoryCells.append(index);
            }
        }
        if (!ownedTerritoryCells.isEmpty()) {
            const int index = ownedTerritoryCells.at(
                QRandomGenerator::global()->bounded(ownedTerritoryCells.size()));
            return QPointF(index % GAME_MAP_WIDTH + 0.5f,
                           index / GAME_MAP_WIDTH + 0.5f);
        }

        const PlayerState* target = livingPlayers.at(
            QRandomGenerator::global()->bounded(livingPlayers.size()));
        const double angle = QRandomGenerator::global()->generateDouble() * 2.0 * M_PI;
        const double distance = QRandomGenerator::global()->generateDouble() * 6.0;
        return QPointF(
            qBound(0.5, target->x + qCos(angle) * distance, GAME_MAP_WIDTH - 0.5),
            qBound(0.5, target->y + qSin(angle) * distance, GAME_MAP_HEIGHT - 0.5));
    }

    return QPointF(QRandomGenerator::global()->generateDouble() * (GAME_MAP_WIDTH - 1) + 0.5,
                   QRandomGenerator::global()->generateDouble() * (GAME_MAP_HEIGHT - 1) + 0.5);
}

void DefenseGameServer::triggerLightningStrike(const QPointF& center, qint64 nowMs)
{
    for (auto player = m_players.begin(); player != m_players.end(); ++player) {
        if (player->online && player->respawnAtMs <= 0
            && distanceBetween(player->x, player->y,
                               center.x(), center.y()) <= GAME_DISASTER_LIGHTNING_RADIUS) {
            damagePlayer(player.value(), GAME_DISASTER_LIGHTNING_PLAYER_DAMAGE, nowMs);
        }
    }

    ZombieState disasterSource;
    const int minX = qMax(0, qFloor(center.x() - GAME_DISASTER_LIGHTNING_RADIUS));
    const int maxX = qMin(GAME_MAP_WIDTH - 1,
                          qFloor(center.x() + GAME_DISASTER_LIGHTNING_RADIUS));
    const int minY = qMax(0, qFloor(center.y() - GAME_DISASTER_LIGHTNING_RADIUS));
    const int maxY = qMin(GAME_MAP_HEIGHT - 1,
                          qFloor(center.y() + GAME_DISASTER_LIGHTNING_RADIUS));
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const CellState& cell = m_cells.at(cellIndex(x, y));
            if (cell.buildingType != _game_building_none
                && distanceBetween(center.x(), center.y(), x + 0.5f, y + 0.5f)
                    <= GAME_DISASTER_LIGHTNING_RADIUS) {
                damageBuilding(disasterSource, x, y,
                               GAME_DISASTER_LIGHTNING_BUILDING_DAMAGE, nowMs);
            }
        }
    }
    for (auto zombie = m_zombies.begin(); zombie != m_zombies.end(); ++zombie) {
        if (zombie->hp > 0
            && distanceBetween(zombie->x, zombie->y,
                               center.x(), center.y()) <= GAME_DISASTER_LIGHTNING_RADIUS) {
            damageZombie(zombie.value(), GAME_DISASTER_LIGHTNING_ZOMBIE_DAMAGE,
                         0, -1, false);
        }
    }
    removeDeadZombies();
    m_lastFlowFieldAtMs = 0;
    sendEvent(0, _game_event_lightning,
              QStringLiteral("灾难事件：闪电击中坐标 (%1, %2)，爆炸对附近玩家、建筑和僵尸造成伤害。")
                  .arg(qFloor(center.x())).arg(qFloor(center.y())),
              center.x(), center.y());
}

DefenseGameServer::ZombieState DefenseGameServer::acquireZombieState()
{
    if (m_zombiePool.isEmpty()) {
        return ZombieState();
    }
    ZombieState zombie = std::move(m_zombiePool.last());
    m_zombiePool.removeLast();
    zombie.id = 0;
    zombie.x = 0.0f;
    zombie.y = 0.0f;
    zombie.hp = 50;
    zombie.maxHp = 50;
    zombie.kind = 0;
    zombie.lastAttackAtMs = 0;
    zombie.path.clear();
    zombie.pathIndex = 0;
    zombie.targetUserId = 0;
    zombie.targetBuildingIndex = -1;
    zombie.targetCell = QPoint(-1, -1);
    zombie.attackRange = kZombieBaseAttackRange;
    zombie.targetSearchRange = kZombieBaseTargetSearchRange;
    zombie.pathUpdatedAtMs = 0;
    zombie.pathUsesBreaches = false;
    zombie.plannedBreaches.clear();
    zombie.plannedPathCost = 0.0f;
    zombie.lastProgressAtMs = 0;
    zombie.lastProgressX = 0.0f;
    zombie.lastProgressY = 0.0f;
    zombie.lastAiUpdateAtMs = 0;
    zombie.lastDamageOwnerId = 0;
    zombie.lastDamageBuildingIndex = -1;
    zombie.lastDamageWasPlayer = false;
    zombie.wasObserved = false;
    zombie.velocityX = 0.0f;
    zombie.velocityY = 0.0f;
    zombie.preferredVelocityX = 0.0f;
    zombie.preferredVelocityY = 0.0f;
    return zombie;
}

void DefenseGameServer::recycleZombieState(ZombieState&& zombie)
{
    zombie.path.clear();
    zombie.plannedBreaches.clear();
    if (m_zombiePool.size() < kMaxActiveZombies) {
        m_zombiePool.append(std::move(zombie));
    }
}

QVector<bool> DefenseGameServer::zombieSpawnAllowedMask() const
{
    QVector<bool> allowed(m_cells.size(), true);
    for (int index = 0; index < m_cells.size(); ++index) {
        const CellState& cell = m_cells.at(index);
        allowed[index] = cell.terrain == _game_terrain_plain
            && cell.buildingType == _game_building_none && cell.zoneId == 0;
    }
    static const QPoint directions[] = {
        QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)
    };
    for (auto zone = m_zones.constBegin(); zone != m_zones.constEnd(); ++zone) {
        for (int index : zone->cells) {
            const int x = index % GAME_MAP_WIDTH;
            const int y = index / GAME_MAP_WIDTH;
            bool boundary = false;
            for (const QPoint& direction : directions) {
                const int nx = x + direction.x();
                const int ny = y + direction.y();
                if (!inBounds(nx, ny)
                    || m_cells.at(cellIndex(nx, ny)).zoneId != zone->id) {
                    boundary = true;
                    break;
                }
            }
            if (!boundary) {
                continue;
            }
            for (int dy = -kZombieZoneSpawnBufferCells;
                 dy <= kZombieZoneSpawnBufferCells; ++dy) {
                for (int dx = -kZombieZoneSpawnBufferCells;
                     dx <= kZombieZoneSpawnBufferCells; ++dx) {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (inBounds(nx, ny)) {
                        allowed[cellIndex(nx, ny)] = false;
                    }
                }
            }
        }
    }
    return allowed;
}

void DefenseGameServer::initializeDormantHordes()
{
    m_dormantZombieDensity.fill(0, GAME_MAP_WIDTH * GAME_MAP_HEIGHT);
    const QVector<bool> allowed = zombieSpawnAllowedMask();
    for (int index = 0; index < allowed.size(); ++index) {
        m_dormantZombieDensity[index] = allowed.at(index)
            ? 2 + ((index * 37 + index / GAME_MAP_WIDTH * 13) % 3) : 0;
    }
}

void DefenseGameServer::replenishDormantHordes(qint64 nowMs)
{
    if (m_dormantZombieDensity.size() != GAME_MAP_WIDTH * GAME_MAP_HEIGHT
        || (m_lastDormantReplenishAtMs > 0
            && nowMs - m_lastDormantReplenishAtMs < kDormantZombieReplenishMs)) {
        return;
    }
    m_lastDormantReplenishAtMs = nowMs;
    const QVector<bool> allowed = zombieSpawnAllowedMask();
    QSet<int> activeCells;
    int allowedCellCount = 0;
    int totalPopulation = m_zombies.size() + m_suspendedZombies.size();
    for (int index = 0; index < allowed.size(); ++index) {
        allowedCellCount += allowed.at(index) ? 1 : 0;
        totalPopulation += index < m_dormantZombieDensity.size()
            ? m_dormantZombieDensity.at(index) : 0;
    }
    const int populationDeficit = allowedCellCount * kDormantZombieBaselineDensity
        - totalPopulation;
    if (populationDeficit <= 0) {
        return;
    }
    for (auto zombie = m_zombies.constBegin(); zombie != m_zombies.constEnd(); ++zombie) {
        if (zombie->hp > 0) {
            activeCells.insert(cellIndex(qBound(0, qFloor(zombie->x), GAME_MAP_WIDTH - 1),
                                         qBound(0, qFloor(zombie->y), GAME_MAP_HEIGHT - 1)));
        }
    }
    for (auto zombie = m_suspendedZombies.constBegin();
         zombie != m_suspendedZombies.constEnd(); ++zombie) {
        if (zombie->hp > 0) {
            activeCells.insert(cellIndex(qBound(0, qFloor(zombie->x), GAME_MAP_WIDTH - 1),
                                         qBound(0, qFloor(zombie->y), GAME_MAP_HEIGHT - 1)));
        }
    }
    QVector<int> vacancies;
    vacancies.reserve(allowed.size());
    for (int index = 0; index < allowed.size(); ++index) {
        if (!allowed.at(index)) {
            m_dormantZombieDensity[index] = 0;
            continue;
        }
        if (m_dormantZombieDensity.at(index) < kDormantZombieBaselineDensity
            && !activeCells.contains(index)) {
            vacancies.append(index);
        }
    }
    const int additions = qMin(qMin(kDormantZombieReplenishBatch, vacancies.size()),
                               populationDeficit);
    for (int i = 0; i < additions; ++i) {
        const int selected = QRandomGenerator::global()->bounded(i, vacancies.size());
        qSwap(vacancies[i], vacancies[selected]);
        ++m_dormantZombieDensity[vacancies.at(i)];
    }
}

void DefenseGameServer::advanceDormantHordes(qint64 nowMs)
{
    if (nowMs >= m_waveAssaultUntilMs || m_dormantZombieDensity.isEmpty()
        || nowMs - m_lastDormantAdvanceAtMs < kDormantZombieAdvanceMs) {
        return;
    }
    m_lastDormantAdvanceAtMs = nowMs;
    const QVector<bool> allowed = zombieSpawnAllowedMask();
    QVector<int> moved(m_dormantZombieDensity.size(), 0);
    for (int index = 0; index < m_dormantZombieDensity.size(); ++index) {
        const int count = m_dormantZombieDensity.at(index);
        if (count <= 0 || !allowed.at(index)) {
            continue;
        }
        QPoint next = index < m_flowNextCells.size()
            ? m_flowNextCells.at(index) : QPoint(-1, -1);
        const int x = index % GAME_MAP_WIDTH;
        const int y = index / GAME_MAP_WIDTH;
        const int sector = sectorIndexForCell(x, y);
        const int nextSector = m_sectorFlowNext.value(sector, -1);
        if (nextSector >= 0 && nextSector != sector) {
            const int sectorX = sector % kFlowSectorColumns;
            const int sectorY = sector / kFlowSectorColumns;
            const int nextSectorX = nextSector % kFlowSectorColumns;
            const int nextSectorY = nextSector / kFlowSectorColumns;
            const QPoint coarseDirection(qBound(-1, nextSectorX - sectorX, 1),
                                         qBound(-1, nextSectorY - sectorY, 1));
            const QPoint coarseNext(x + coarseDirection.x(),
                                    y + coarseDirection.y());
            if (inBounds(coarseNext.x(), coarseNext.y())) {
                const int coarseIndex = cellIndex(coarseNext.x(), coarseNext.y());
                const float currentCost = m_flowCosts.value(index,
                    std::numeric_limits<float>::max());
                const float coarseCost = m_flowCosts.value(coarseIndex,
                    std::numeric_limits<float>::max());
                if (allowed.value(coarseIndex, false) && qIsFinite(coarseCost)
                    && (coarseCost <= currentCost + 2.0f || !inBounds(next.x(), next.y()))) {
                    next = coarseNext;
                }
            }
        }
        const int nextIndex = inBounds(next.x(), next.y())
            ? cellIndex(next.x(), next.y()) : index;
        moved[allowed.value(nextIndex, false) ? nextIndex : index] += count;
    }
    m_dormantZombieDensity = std::move(moved);
}

void DefenseGameServer::materializeNearbyHordes(qint64 nowMs, bool force)
{
    if ((m_dormantZombieDensity.isEmpty() && m_suspendedZombies.isEmpty())
        || m_zombies.size() >= kMaxActiveZombies
        || (!force && m_lastDormantMaterializeAtMs > 0
            && nowMs - m_lastDormantMaterializeAtMs < kDormantZombieMaterializeMs)) {
        return;
    }
    m_lastDormantMaterializeAtMs = nowMs;
    const bool assaultActive = nowMs < m_waveAssaultUntilMs;
    const float activeDistance = assaultActive
        ? kZombieMidSimulationDistance : kZombieNearSimulationDistance;

    struct SuspendedCandidate {
        bool visible = false;
        float distance = 0.0f;
        int zombieId = 0;
    };
    QVector<SuspendedCandidate> suspendedCandidates;
    suspendedCandidates.reserve(m_suspendedZombies.size());
    for (auto zombie = m_suspendedZombies.constBegin();
         zombie != m_suspendedZombies.constEnd(); ++zombie) {
        const bool visible = zombieIsVisibleToAnyPlayer(zombie.value());
        const float distance = distanceToNearestPlayerForce(zombie->x, zombie->y);
        if (visible || distance <= activeDistance) {
            suspendedCandidates.append({visible, distance, zombie.key()});
        }
    }
    std::sort(suspendedCandidates.begin(), suspendedCandidates.end(),
              [](const SuspendedCandidate& left, const SuspendedCandidate& right) {
        if (left.visible != right.visible) {
            return left.visible;
        }
        return left.distance < right.distance;
    });
    for (const SuspendedCandidate& candidate : suspendedCandidates) {
        if (m_zombies.size() >= kMaxActiveZombies) {
            break;
        }
        auto zombie = m_suspendedZombies.find(candidate.zombieId);
        if (zombie == m_suspendedZombies.end()) {
            continue;
        }
        ZombieState restored = std::move(zombie.value());
        m_suspendedZombies.erase(zombie);
        restored.path.clear();
        restored.pathIndex = 0;
        restored.targetUserId = 0;
        restored.targetBuildingIndex = -1;
        restored.targetCell = QPoint(-1, -1);
        restored.pathUpdatedAtMs = 0;
        restored.lastAiUpdateAtMs = 0;
        restored.lastProgressAtMs = nowMs;
        restored.lastProgressX = restored.x;
        restored.lastProgressY = restored.y;
        m_zombies.insert(restored.id, std::move(restored));
    }
    if (m_zombies.size() >= kMaxActiveZombies || m_dormantZombieDensity.isEmpty()) {
        return;
    }

    const QVector<bool> allowed = zombieSpawnAllowedMask();
    QVector<QPair<float, int>> candidates;
    for (int index = 0; index < m_dormantZombieDensity.size(); ++index) {
        if (m_dormantZombieDensity.at(index) <= 0 || !allowed.at(index)) {
            continue;
        }
        const float distance = distanceToNearestPlayerForce(index % GAME_MAP_WIDTH + 0.5f,
                                                            index / GAME_MAP_WIDTH + 0.5f);
        if (distance >= kZombieMaterializationClearance
            && distance <= (assaultActive ? kZombieMidSimulationDistance
                                          : kZombieNearSimulationDistance)) {
            candidates.append({distance, index});
        }
    }
    std::sort(candidates.begin(), candidates.end());
    int activationBudget = assaultActive ? 18 : 4;
    for (const auto& candidate : candidates) {
        if (activationBudget <= 0 || m_zombies.size() >= kMaxActiveZombies) {
            break;
        }
        const int index = candidate.second;
        if (m_dormantZombieDensity[index] <= 0) {
            continue;
        }
        --m_dormantZombieDensity[index];
        const int kind = m_wave >= 3 && (m_nextZombieId % 11 == 0)
            ? GAME_ZOMBIE_KIND_GIANT
            : m_wave >= 2 && (m_nextZombieId % 4 == 0)
                ? GAME_ZOMBIE_KIND_ELITE : GAME_ZOMBIE_KIND_NORMAL;
        spawnZombieAt(QPointF(index % GAME_MAP_WIDTH + 0.5f,
                              index / GAME_MAP_WIDTH + 0.5f),
                      45 + qMax(1, m_wave) * 8, kind);
        --activationBudget;
    }
}

void DefenseGameServer::dematerializeDistantZombies(qint64 nowMs)
{
    if (m_zombies.isEmpty()) {
        return;
    }
    const float activeDistance = nowMs < m_waveAssaultUntilMs
        ? kZombieMidSimulationDistance : kZombieNearSimulationDistance;
    QVector<int> recycledIds;
    recycledIds.reserve(m_zombies.size());
    for (auto zombie = m_zombies.begin(); zombie != m_zombies.end(); ++zombie) {
        if (zombieIsVisibleToAnyPlayer(zombie.value())) {
            zombie->wasObserved = true;
        }
        if (zombie->hp <= 0
            || (distanceToNearestPlayerForce(zombie->x, zombie->y) > activeDistance + 8.0f
                && !zombie->wasObserved)) {
            recycledIds.append(zombie.key());
        }
    }
    for (int zombieId : recycledIds) {
        auto zombie = m_zombies.find(zombieId);
        if (zombie == m_zombies.end()) {
            continue;
        }
        ZombieState recycled = std::move(zombie.value());
        m_zombies.erase(zombie);
        if (recycled.hp > 0) {
            recycled.path.clear();
            recycled.pathIndex = 0;
            recycled.plannedBreaches.clear();
            recycled.targetUserId = 0;
            recycled.targetBuildingIndex = -1;
            recycled.targetCell = QPoint(-1, -1);
            m_suspendedZombies.insert(recycled.id, std::move(recycled));
        } else {
            recycleZombieState(std::move(recycled));
        }
    }
    if (!recycledIds.isEmpty()) {
        rebuildZombieSpatialIndex();
    }
}

bool DefenseGameServer::zombieIsVisibleToAnyPlayer(const ZombieState& zombie) const
{
    for (auto player = m_players.constBegin(); player != m_players.constEnd(); ++player) {
        if (player->online
            && distanceBetween(zombie.x, zombie.y, player->x, player->y) <= 8.5f) {
            return true;
        }
    }

    const int zombieX = qBound(0, qFloor(zombie.x), GAME_MAP_WIDTH - 1);
    const int zombieY = qBound(0, qFloor(zombie.y), GAME_MAP_HEIGHT - 1);
    const CellState& zombieCell = m_cells.at(cellIndex(zombieX, zombieY));
    if (zombieCell.zoneId > 0) {
        const ZoneState* zone = zoneById(zombieCell.zoneId);
        const PlayerState* owner = zone ? playerForUser(zone->ownerId) : nullptr;
        if (owner && owner->online) {
            return true;
        }
    }

    constexpr float buildingVisionRadius = 4.0f;
    const int minX = qMax(0, qFloor(zombie.x - buildingVisionRadius));
    const int maxX = qMin(GAME_MAP_WIDTH - 1, qCeil(zombie.x + buildingVisionRadius));
    const int minY = qMax(0, qFloor(zombie.y - buildingVisionRadius));
    const int maxY = qMin(GAME_MAP_HEIGHT - 1, qCeil(zombie.y + buildingVisionRadius));
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const CellState& cell = m_cells.at(cellIndex(x, y));
            if (cell.buildingType == _game_building_none || cell.buildingOwnerId <= 0
                || distanceBetween(zombie.x, zombie.y, x + 0.5f, y + 0.5f)
                    > buildingVisionRadius) {
                continue;
            }
            const PlayerState* owner = playerForUser(cell.buildingOwnerId);
            if (owner && owner->online) {
                return true;
            }
        }
    }
    return false;
}

void DefenseGameServer::spawnWave(qint64 nowMs)
{
    bool hasOnlinePlayer = false;
    for (auto player = m_players.constBegin(); player != m_players.constEnd(); ++player) {
        if (player->online && player->respawnAtMs <= 0) {
            hasOnlinePlayer = true;
            break;
        }
    }
    if (!hasOnlinePlayer) {
        m_nextWaveAtMs = nowMs + 1000;
        return;
    }

    ++m_wave;
    m_waveAssaultUntilMs = nowMs + kWaveAssaultDurationMs;
    m_lastFlowFieldAtMs = 0;
    materializeNearbyHordes(nowMs, true);
    m_nextWaveAtMs = nowMs + kWaveIntervalMs;
    int dormantPopulation = 0;
    for (int count : m_dormantZombieDensity) {
        dormantPopulation += count;
    }
    sendEvent(0, _game_event_wave,
              QStringLiteral("第 %1 波开始，全地图 %2 个尸群正向玩家势力区域推进。")
                  .arg(m_wave)
                  .arg(dormantPopulation + m_zombies.size()
                       + m_suspendedZombies.size()));
}

void DefenseGameServer::spawnZombie(int hp, int kind)
{
    const QVector<bool> allowed = zombieSpawnAllowedMask();
    QPointF spawn = zombieSpawnPoint();
    for (int attempt = 0; attempt < 200; ++attempt) {
        const int index = QRandomGenerator::global()->bounded(allowed.size());
        if (allowed.at(index)) {
            spawn = QPointF(index % GAME_MAP_WIDTH + 0.5f,
                            index / GAME_MAP_WIDTH + 0.5f);
            break;
        }
    }
    spawnZombieAt(spawn, hp, kind);
}

void DefenseGameServer::spawnZombieAt(const QPointF& position, int hp, int kind)
{
    if (m_zombies.size() >= kMaxActiveZombies) {
        return;
    }
    ZombieState zombie = acquireZombieState();
    zombie.id = m_nextZombieId++;
    zombie.x = static_cast<float>(position.x());
    zombie.y = static_cast<float>(position.y());
    zombie.hp = hp + (kind == GAME_ZOMBIE_KIND_GIANT
        ? GAME_GIANT_ZOMBIE_HP_BONUS : kind * 30);
    zombie.maxHp = zombie.hp;
    zombie.kind = kind;
    zombie.attackRange = kind == GAME_ZOMBIE_KIND_GIANT ? 1.05f
        : kZombieBaseAttackRange + kind * kZombieEliteAttackRangeBonus;
    zombie.targetSearchRange = kZombieBaseTargetSearchRange
        + (kind == GAME_ZOMBIE_KIND_GIANT ? 6.0f
                                           : kind * kZombieEliteTargetSearchRangeBonus);
    m_zombies.insert(zombie.id, zombie);
}

QVector<QPointF> DefenseGameServer::safeBoundaryPoints() const
{
    QVector<QPointF> points;
    static const QPoint directions[] = {QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)};
    for (auto zone = m_zones.constBegin(); zone != m_zones.constEnd(); ++zone) {
        for (int index : zone->cells) {
            const int x = index % GAME_MAP_WIDTH;
            const int y = index / GAME_MAP_WIDTH;
            bool boundary = false;
            for (const QPoint& direction : directions) {
                const int nx = x + direction.x();
                const int ny = y + direction.y();
                if (!inBounds(nx, ny) || m_cells.at(cellIndex(nx, ny)).zoneId != zone->id) {
                    boundary = true;
                    break;
                }
            }
            if (boundary) {
                points.append(QPointF(x + 0.5, y + 0.5));
            }
        }
    }

    if (!points.isEmpty()) {
        return points;
    }

    float minX = GAME_MAP_WIDTH / 2.0f;
    float maxX = minX;
    float minY = GAME_MAP_HEIGHT / 2.0f;
    float maxY = minY;
    bool foundPlayer = false;
    for (auto player = m_players.constBegin(); player != m_players.constEnd(); ++player) {
        if (!player->online || player->respawnAtMs > 0) {
            continue;
        }
        if (!foundPlayer) {
            minX = maxX = player->x;
            minY = maxY = player->y;
            foundPlayer = true;
        } else {
            minX = qMin(minX, player->x);
            maxX = qMax(maxX, player->x);
            minY = qMin(minY, player->y);
            maxY = qMax(maxY, player->y);
        }
    }
    constexpr float fallbackMargin = 2.0f;
    minX -= fallbackMargin;
    maxX += fallbackMargin;
    minY -= fallbackMargin;
    maxY += fallbackMargin;
    const int samples = 8;
    for (int i = 0; i < samples; ++i) {
        const float t = static_cast<float>(i) / (samples - 1);
        points.append(QPointF(minX + (maxX - minX) * t, minY));
        points.append(QPointF(minX + (maxX - minX) * t, maxY));
        points.append(QPointF(minX, minY + (maxY - minY) * t));
        points.append(QPointF(maxX, minY + (maxY - minY) * t));
    }
    return points;
}

QPointF DefenseGameServer::zombieSpawnPoint() const
{
    QVector<QPointF> boundary;
    QPointF center;
    int centerCount = 0;

    QVector<int> populatedZoneIds;
    for (auto zone = m_zones.constBegin(); zone != m_zones.constEnd(); ++zone) {
        if (!zone->cells.isEmpty()) {
            populatedZoneIds.append(zone->id);
        }
    }
    if (!populatedZoneIds.isEmpty()) {
        const int zoneId = populatedZoneIds.at(
            QRandomGenerator::global()->bounded(populatedZoneIds.size()));
        const ZoneState* zone = zoneById(zoneId);
        static const QPoint directions[] = {
            QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)
        };
        for (int index : zone->cells) {
            const int x = index % GAME_MAP_WIDTH;
            const int y = index / GAME_MAP_WIDTH;
            center += QPointF(x + 0.5, y + 0.5);
            ++centerCount;
            for (const QPoint& direction : directions) {
                const int nx = x + direction.x();
                const int ny = y + direction.y();
                if (!inBounds(nx, ny) || m_cells.at(cellIndex(nx, ny)).zoneId != zoneId) {
                    boundary.append(QPointF(x + 0.5, y + 0.5));
                    break;
                }
            }
        }
    } else {
        boundary = safeBoundaryPoints();
        for (auto player = m_players.constBegin(); player != m_players.constEnd(); ++player) {
            if (player->online) {
                center += QPointF(player->x, player->y);
                ++centerCount;
            }
        }
    }
    if (boundary.isEmpty()) {
        boundary = safeBoundaryPoints();
    }
    const QPointF selected = boundary.at(QRandomGenerator::global()->bounded(boundary.size()));
    if (centerCount > 0) {
        center /= centerCount;
    } else {
        center = QPointF(GAME_MAP_WIDTH / 2.0, GAME_MAP_HEIGHT / 2.0);
    }

    QPointF outward = normalizedVector(selected.x() - center.x(), selected.y() - center.y());
    if (qFuzzyIsNull(outward.x()) && qFuzzyIsNull(outward.y())) {
        const double angle = QRandomGenerator::global()->generateDouble() * 2.0 * M_PI;
        outward = QPointF(qCos(angle), qSin(angle));
    }
    const double angleJitter = (QRandomGenerator::global()->generateDouble() - 0.5) * 0.28;
    const QPointF rotated(outward.x() * qCos(angleJitter) - outward.y() * qSin(angleJitter),
                          outward.x() * qSin(angleJitter) + outward.y() * qCos(angleJitter));
    const QPointF candidate = selected + rotated * m_zombieSpawnDistanceCells;
    return findSafePositionNear(candidate.x(), candidate.y());
}

void DefenseGameServer::sendSnapshot(ConnectionId socket)
{
    if (!m_network || !m_sessions.contains(socket)) {
        return;
    }
    const STRU_GAME_STATE_RS snapshot = makeSnapshot();
    const QByteArray packet = compressGameSnapshot(snapshot);
    m_network->sendData(socket, packet.constData(), packet.size());
}

void DefenseGameServer::broadcastSnapshot()
{
    const QList<ConnectionId> sockets = m_sessions.keys();
    if (sockets.isEmpty() || !m_network) {
        return;
    }
    const STRU_GAME_STATE_RS snapshot = makeSnapshot();
    const QByteArray packet = compressGameSnapshot(snapshot);
    for (ConnectionId socket : sockets) {
        m_network->sendData(socket, packet.constData(), packet.size());
    }
}

void DefenseGameServer::sendEvent(qint64 userId, int eventType, const QString& message,
                                  float x, float y)
{
    if (!m_network) {
        return;
    }
    STRU_GAME_EVENT_RS event;
    event.m_userId = userId;
    event.m_eventType = static_cast<std::uint8_t>(eventType);
    event.m_x = x;
    event.m_y = y;
    copyText(event.m_message, GAME_EVENT_SIZE, message);
    for (auto it = m_sessions.constBegin(); it != m_sessions.constEnd(); ++it) {
        if (userId == 0 || it.value() == userId) {
            m_network->sendData(it.key(), reinterpret_cast<const char*>(&event), sizeof(event));
        }
    }
}

STRU_GAME_STATE_RS DefenseGameServer::makeSnapshot() const
{
    STRU_GAME_STATE_RS snapshot;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    snapshot.m_serverTimeMs = now;
    snapshot.m_wave = m_wave;
    snapshot.m_nextWaveSeconds = static_cast<int>(
        qMax<qint64>(0, (m_nextWaveAtMs - now + 999) / 1000));

    for (int i = 0; i < m_cells.size(); ++i) {
        const CellState& source = m_cells.at(i);
        GameCellInfo& target = snapshot.m_cells[i];
        target.m_zoneId = source.zoneId;
        target.m_buildingOwnerId = source.buildingOwnerId;
        target.m_buildingHp = static_cast<std::int16_t>(source.buildingHp);
        target.m_terrain = static_cast<std::uint8_t>(source.terrain);
        target.m_resourceType = static_cast<std::uint8_t>(source.resourceType);
        target.m_resourceAmount = static_cast<std::int16_t>(source.resourceAmount);
        target.m_buildingType = static_cast<std::uint8_t>(source.buildingType);
        target.m_buildingLevel = static_cast<std::uint8_t>(source.buildingLevel);
        target.m_buildingExp = static_cast<std::int16_t>(source.buildingExp);
        target.m_dormantZombieDensity = static_cast<std::uint8_t>(qBound(
            0, i < m_dormantZombieDensity.size() ? m_dormantZombieDensity.at(i) : 0, 255));
        const QPoint current(i % GAME_MAP_WIDTH, i / GAME_MAP_WIDTH);
        const QPoint next = m_flowNextCells.value(i, QPoint(-1, -1));
        if (next.x() >= 0 && next.y() >= 0 && next != current) {
            target.m_dormantFlowX = static_cast<std::int8_t>(
                qBound(-1, next.x() - current.x(), 1));
            target.m_dormantFlowY = static_cast<std::int8_t>(
                qBound(-1, next.y() - current.y(), 1));
        }
    }

    int playerIndex = 0;
    auto appendPlayer = [&](const PlayerState& source) {
        if (playerIndex >= GAME_MAX_PLAYERS) {
            return;
        }
        GamePlayerInfo& target = snapshot.m_players[playerIndex++];
        target.m_userId = source.userId;
        copyText(target.m_userName, GAME_NAME_SIZE, source.userName);
        target.m_x = source.x;
        target.m_y = source.y;
        target.m_hp = source.hp;
        target.m_maxHp = source.maxHp;
        target.m_coins = source.coins;
        target.m_stone = source.stone;
        target.m_aluminum = source.aluminum;
        target.m_iron = source.iron;
        target.m_coal = source.coal;
        target.m_oil = source.oil;
        target.m_respawnRemainingSeconds = source.respawnAtMs > now
            ? static_cast<std::int32_t>((source.respawnAtMs - now + 999) / 1000) : 0;
        target.m_invulnerableRemainingSeconds = source.invulnerableUntilMs > now
            ? static_cast<std::int32_t>((source.invulnerableUntilMs - now + 999) / 1000) : 0;
        target.m_level = static_cast<std::int16_t>(source.level);
        target.m_kills = source.kills;
        target.m_experience = source.experience;
        target.m_experienceToNextLevel = source.level < GAME_MAX_LEVEL
            ? playerExperienceRequired(source.level) : 0;
        target.m_respawnZoneId = source.respawnZoneId;
        const PopulationSupport support = populationSupport(source.userId);
        target.m_population = source.population;
        target.m_populationCapacity = support.capacity;
        target.m_populationUsed = turretPopulationUsed(source.userId);
        target.m_foodCapacity = support.food;
        target.m_waterCapacity = support.water;
        target.m_online = source.online ? 1 : 0;
        target.m_dead = source.respawnAtMs > 0 ? 1 : 0;
        target.m_colorR = source.color.red();
        target.m_colorG = source.color.green();
        target.m_colorB = source.color.blue();
    };
    for (auto it = m_players.constBegin(); it != m_players.constEnd() && playerIndex < GAME_MAX_PLAYERS; ++it) {
        if (it->online) {
            appendPlayer(it.value());
        }
    }
    snapshot.m_playerCount = playerIndex;

    QVector<const ZombieState*> visibleZombies;
    visibleZombies.reserve(m_zombies.size());
    for (auto zombie = m_zombies.constBegin(); zombie != m_zombies.constEnd(); ++zombie) {
        if (zombie->hp > 0) {
            visibleZombies.append(&zombie.value());
        }
    }
    std::sort(visibleZombies.begin(), visibleZombies.end(), [this](const ZombieState* left,
                                                                  const ZombieState* right) {
        return distanceToNearestPlayerForce(left->x, left->y)
            < distanceToNearestPlayerForce(right->x, right->y);
    });
    int zombieIndex = 0;
    for (const ZombieState* source : visibleZombies) {
        if (zombieIndex >= GAME_MAX_ZOMBIES) {
            break;
        }
        GameZombieInfo& target = snapshot.m_zombies[zombieIndex++];
        target.m_zombieId = source->id;
        target.m_x = source->x;
        target.m_y = source->y;
        target.m_hp = source->hp;
        target.m_maxHp = source->maxHp;
        target.m_kind = source->kind;
    }
    snapshot.m_zombieCount = zombieIndex;

    int zoneIndex = 0;
    for (auto it = m_zones.constBegin(); it != m_zones.constEnd() && zoneIndex < GAME_MAX_ZONES; ++it) {
        const ZoneState& source = it.value();
        GameZoneInfo& target = snapshot.m_zones[zoneIndex++];
        target.m_zoneId = source.id;
        target.m_ownerId = source.ownerId;
        target.m_cellCount = source.cells.size();
        target.m_stabilizeRemainingSeconds = source.state == _game_zone_stabilizing
            ? qMax<qint64>(0, (source.stabilizeAtMs - now + 999) / 1000) : 0;
        target.m_reinforceRemainingSeconds = qMax<qint64>(0, (source.reinforceUntilMs - now + 999) / 1000);
        target.m_state = source.state;
        target.m_shieldLayers = source.shieldLayers;
        target.m_colorR = source.color.red();
        target.m_colorG = source.color.green();
        target.m_colorB = source.color.blue();
    }
    snapshot.m_zoneCount = zoneIndex;

    QHash<qint64, int> permanentCells;
    QHash<qint64, QColor> territoryColors;
    for (auto zone = m_zones.constBegin(); zone != m_zones.constEnd(); ++zone) {
        if (zone->state != _game_zone_permanent || zone->ownerId <= 0) {
            continue;
        }
        permanentCells[zone->ownerId] += zone->cells.size();
        territoryColors.insert(zone->ownerId, zone->color);
    }
    auto rankedOwners = permanentCells.keys();
    std::sort(rankedOwners.begin(), rankedOwners.end(), [&](qint64 left, qint64 right) {
        if (permanentCells.value(left) != permanentCells.value(right)) {
            return permanentCells.value(left) > permanentCells.value(right);
        }
        return left < right;
    });
    int rankIndex = 0;
    for (qint64 ownerId : rankedOwners) {
        if (rankIndex >= GAME_MAX_PLAYERS) {
            break;
        }
        GameTerritoryRankInfo& target = snapshot.m_territoryRanks[rankIndex++];
        target.m_userId = ownerId;
        if (const PlayerState* player = playerForUser(ownerId)) {
            copyText(target.m_userName, GAME_NAME_SIZE, player->userName);
            target.m_colorR = player->color.red();
            target.m_colorG = player->color.green();
            target.m_colorB = player->color.blue();
        } else {
            copyText(target.m_userName, GAME_NAME_SIZE, QString::number(ownerId));
            const QColor color = territoryColors.value(ownerId, QColor(38, 166, 154));
            target.m_colorR = color.red();
            target.m_colorG = color.green();
            target.m_colorB = color.blue();
        }
        target.m_permanentCellCount = permanentCells.value(ownerId);
    }
    snapshot.m_territoryRankCount = rankIndex;
    return snapshot;
}

bool DefenseGameServer::loadWorld()
{
    std::list<std::string> rows;
    const QString sql = QStringLiteral(
        "select state_json::text from defense_game_world where world_id = %1")
        .arg(m_worldId);
    if (!m_database->SelectMySql(sql.toUtf8().constData(), 1, rows)
        || rows.empty()) {
        generateMapFeatures();
        return true;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(rows.front()), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }
    const QJsonObject root = document.object();
    const int schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt();
    if (schemaVersion < 3 || schemaVersion > kWorldSchemaVersion) {
        return false;
    }
    m_wave = root.value(QStringLiteral("wave")).toInt();
    m_nextWaveAtMs = root.value(QStringLiteral("nextWaveAtMs")).toVariant().toLongLong();
    m_nextZombieId = qMax(1, root.value(QStringLiteral("nextZombieId")).toInt(1));
    m_nextZoneId = qMax(1, root.value(QStringLiteral("nextZoneId")).toInt(1));

    const QJsonArray cells = root.value(QStringLiteral("cells")).toArray();
    if (cells.size() != GAME_MAP_WIDTH * GAME_MAP_HEIGHT) {
        return false;
    }
    for (int i = 0; i < cells.size(); ++i) {
        const QJsonObject object = cells.at(i).toObject();
        CellState& cell = m_cells[i];
        cell.terrain = object.value(QStringLiteral("terrain")).toInt();
        cell.resourceType = object.value(QStringLiteral("resourceType")).toInt();
        cell.resourceAmount = qMax(0, object.value(QStringLiteral("resourceAmount")).toInt());
        cell.buildingType = object.value(QStringLiteral("buildingType")).toInt();
        cell.buildingOwnerId = object.value(QStringLiteral("buildingOwnerId")).toVariant().toLongLong();
        cell.buildingHp = object.value(QStringLiteral("buildingHp")).toInt();
        cell.buildingLevel = qBound(
            cell.buildingType == _game_building_none ? 0 : 1,
            object.value(QStringLiteral("buildingLevel")).toInt(
                cell.buildingType == _game_building_none ? 0 : 1),
            cell.buildingType == _game_building_none ? 0 : GAME_MAX_LEVEL);
        cell.buildingExp = object.value(QStringLiteral("buildingExp")).toInt();
        cell.zoneId = object.value(QStringLiteral("zoneId")).toInt();
    }

    const QJsonArray players = root.value(QStringLiteral("players")).toArray();
    for (const QJsonValue& value : players) {
        const QJsonObject object = value.toObject();
        PlayerState player;
        player.userId = object.value(QStringLiteral("userId")).toVariant().toLongLong();
        player.userName = object.value(QStringLiteral("userName")).toString();
        player.x = static_cast<float>(object.value(QStringLiteral("x")).toDouble());
        player.y = static_cast<float>(object.value(QStringLiteral("y")).toDouble());
        player.spawnX = static_cast<float>(object.value(QStringLiteral("spawnX")).toDouble(player.x));
        player.spawnY = static_cast<float>(object.value(QStringLiteral("spawnY")).toDouble(player.y));
        player.respawnZoneId = object.value(QStringLiteral("respawnZoneId")).toInt();
        player.hp = object.value(QStringLiteral("hp")).toInt(100);
        player.maxHp = object.value(QStringLiteral("maxHp")).toInt(100);
        player.coins = object.value(QStringLiteral("coins")).toInt(
            object.value(QStringLiteral("materials")).toInt(GAME_PLAYER_START_COINS));
        player.stone = qMax(0, object.value(QStringLiteral("stone")).toInt(
            schemaVersion < 5 ? GAME_PLAYER_START_STONE : 0));
        player.aluminum = qMax(0, object.value(QStringLiteral("aluminum")).toInt());
        player.iron = qMax(0, object.value(QStringLiteral("iron")).toInt());
        player.coal = qMax(0, object.value(QStringLiteral("coal")).toInt());
        player.oil = qMax(0, object.value(QStringLiteral("oil")).toInt());
        player.population = qMax(0, object.value(QStringLiteral("population"))
                                      .toInt(GAME_INITIAL_POPULATION));
        player.level = qBound(1, object.value(QStringLiteral("level")).toInt(1),
                              GAME_MAX_LEVEL);
        player.kills = object.value(QStringLiteral("kills")).toInt();
        player.experience = qMax(0, object.value(QStringLiteral("experience")).toInt());
        if (player.level >= GAME_MAX_LEVEL) {
            player.experience = 0;
        } else {
            player.experience = qMin(player.experience,
                                     playerExperienceRequired(player.level) - 1);
        }
        player.respawnAtMs = object.value(QStringLiteral("respawnAtMs")).toVariant().toLongLong();
        player.invulnerableUntilMs =
            object.value(QStringLiteral("invulnerableUntilMs")).toVariant().toLongLong();
        if (player.respawnAtMs <= 0 && player.hp <= 0) {
            player.hp = player.maxHp;
        }
        player.color = QColor(object.value(QStringLiteral("color")).toString(QStringLiteral("#26a69a")));
        player.online = false;
        if (player.userId > 0 && inBounds(qFloor(player.x), qFloor(player.y))) {
            m_players.insert(player.userId, player);
        }
    }

    const QJsonArray zombies = root.value(QStringLiteral("zombies")).toArray();
    for (const QJsonValue& value : zombies) {
        const QJsonObject object = value.toObject();
        ZombieState zombie;
        zombie.id = object.value(QStringLiteral("id")).toInt();
        zombie.x = static_cast<float>(object.value(QStringLiteral("x")).toDouble());
        zombie.y = static_cast<float>(object.value(QStringLiteral("y")).toDouble());
        zombie.hp = object.value(QStringLiteral("hp")).toInt();
        zombie.maxHp = object.value(QStringLiteral("maxHp")).toInt();
        zombie.kind = object.value(QStringLiteral("kind")).toInt();
        zombie.attackRange = static_cast<float>(object.value(QStringLiteral("attackRange")).toDouble(
            kZombieBaseAttackRange + zombie.kind * kZombieEliteAttackRangeBonus));
        zombie.targetSearchRange = static_cast<float>(
            object.value(QStringLiteral("targetSearchRange")).toDouble(
                kZombieBaseTargetSearchRange
                    + zombie.kind * kZombieEliteTargetSearchRangeBonus));
        zombie.wasObserved = object.value(QStringLiteral("wasObserved")).toBool(false);
        zombie.velocityX = static_cast<float>(object.value(QStringLiteral("velocityX")).toDouble());
        zombie.velocityY = static_cast<float>(object.value(QStringLiteral("velocityY")).toDouble());
        zombie.preferredVelocityX = static_cast<float>(
            object.value(QStringLiteral("preferredVelocityX")).toDouble());
        zombie.preferredVelocityY = static_cast<float>(
            object.value(QStringLiteral("preferredVelocityY")).toDouble());
        if (zombie.id > 0 && zombie.hp > 0 && qIsFinite(zombie.x) && qIsFinite(zombie.y)) {
            m_zombies.insert(zombie.id, zombie);
        }
    }

    const QJsonArray suspendedZombies = root.value(QStringLiteral("suspendedZombies")).toArray();
    for (const QJsonValue& value : suspendedZombies) {
        const QJsonObject object = value.toObject();
        ZombieState zombie;
        zombie.id = object.value(QStringLiteral("id")).toInt();
        zombie.x = static_cast<float>(object.value(QStringLiteral("x")).toDouble());
        zombie.y = static_cast<float>(object.value(QStringLiteral("y")).toDouble());
        zombie.hp = object.value(QStringLiteral("hp")).toInt();
        zombie.maxHp = object.value(QStringLiteral("maxHp")).toInt();
        zombie.kind = object.value(QStringLiteral("kind")).toInt();
        zombie.attackRange = static_cast<float>(object.value(QStringLiteral("attackRange")).toDouble(
            kZombieBaseAttackRange + zombie.kind * kZombieEliteAttackRangeBonus));
        zombie.targetSearchRange = static_cast<float>(
            object.value(QStringLiteral("targetSearchRange")).toDouble(
                kZombieBaseTargetSearchRange
                    + zombie.kind * kZombieEliteTargetSearchRangeBonus));
        zombie.wasObserved = object.value(QStringLiteral("wasObserved")).toBool(false);
        zombie.velocityX = static_cast<float>(object.value(QStringLiteral("velocityX")).toDouble());
        zombie.velocityY = static_cast<float>(object.value(QStringLiteral("velocityY")).toDouble());
        zombie.preferredVelocityX = static_cast<float>(
            object.value(QStringLiteral("preferredVelocityX")).toDouble());
        zombie.preferredVelocityY = static_cast<float>(
            object.value(QStringLiteral("preferredVelocityY")).toDouble());
        if (zombie.id > 0 && zombie.hp > 0 && qIsFinite(zombie.x) && qIsFinite(zombie.y)
            && !m_zombies.contains(zombie.id)) {
            m_suspendedZombies.insert(zombie.id, zombie);
        }
    }

    const QJsonArray dormantZombies = root.value(QStringLiteral("dormantZombies")).toArray();
    if (!dormantZombies.isEmpty()) {
        m_dormantZombieDensity.fill(0, GAME_MAP_WIDTH * GAME_MAP_HEIGHT);
        for (const QJsonValue& value : dormantZombies) {
            const QJsonObject object = value.toObject();
            const int index = object.value(QStringLiteral("cell")).toInt(-1);
            const int count = qMax(0, object.value(QStringLiteral("count")).toInt());
            if (index >= 0 && index < m_dormantZombieDensity.size() && count > 0) {
                m_dormantZombieDensity[index] = count;
            }
        }
    }

    const QJsonArray zones = root.value(QStringLiteral("zones")).toArray();
    for (const QJsonValue& value : zones) {
        const QJsonObject object = value.toObject();
        ZoneState zone;
        zone.id = object.value(QStringLiteral("id")).toInt();
        zone.ownerId = object.value(QStringLiteral("ownerId")).toVariant().toLongLong();
        zone.color = QColor(object.value(QStringLiteral("color")).toString(QStringLiteral("#26a69a")));
        zone.state = object.value(QStringLiteral("state")).toInt(_game_zone_stabilizing);
        zone.shieldLayers = object.value(QStringLiteral("shieldLayers")).toInt();
        zone.claimedAtMs = object.value(QStringLiteral("claimedAtMs")).toVariant().toLongLong();
        zone.stabilizeAtMs = object.value(QStringLiteral("stabilizeAtMs")).toVariant().toLongLong();
        if (zone.state == _game_zone_stabilizing && zone.claimedAtMs > 0) {
            zone.stabilizeAtMs = qMin(zone.stabilizeAtMs,
                                      zone.claimedAtMs + kStabilizationMs);
        }
        zone.reinforceUntilMs = object.value(QStringLiteral("reinforceUntilMs")).toVariant().toLongLong();
        if (zone.id > 0) {
            m_zones.insert(zone.id, zone);
        }
    }
    rebuildZoneCells();
    if (schemaVersion < 5) {
        generateMapFeatures();
    }
    return true;
}

bool DefenseGameServer::saveWorld()
{
    if (!m_database) {
        return false;
    }
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), kWorldSchemaVersion);
    root.insert(QStringLiteral("wave"), m_wave);
    root.insert(QStringLiteral("nextWaveAtMs"), QString::number(m_nextWaveAtMs));
    root.insert(QStringLiteral("nextZombieId"), m_nextZombieId);
    root.insert(QStringLiteral("nextZoneId"), m_nextZoneId);

    QJsonArray cells;
    for (const CellState& cell : m_cells) {
        QJsonObject object;
        object.insert(QStringLiteral("terrain"), cell.terrain);
        object.insert(QStringLiteral("resourceType"), cell.resourceType);
        object.insert(QStringLiteral("resourceAmount"), cell.resourceAmount);
        object.insert(QStringLiteral("buildingType"), cell.buildingType);
        object.insert(QStringLiteral("buildingOwnerId"), QString::number(cell.buildingOwnerId));
        object.insert(QStringLiteral("buildingHp"), cell.buildingHp);
        object.insert(QStringLiteral("buildingLevel"), cell.buildingLevel);
        object.insert(QStringLiteral("buildingExp"), cell.buildingExp);
        object.insert(QStringLiteral("zoneId"), cell.zoneId);
        cells.append(object);
    }
    root.insert(QStringLiteral("cells"), cells);

    QJsonArray players;
    for (auto it = m_players.constBegin(); it != m_players.constEnd(); ++it) {
        const PlayerState& player = it.value();
        QJsonObject object;
        object.insert(QStringLiteral("userId"), QString::number(player.userId));
        object.insert(QStringLiteral("userName"), player.userName);
        object.insert(QStringLiteral("x"), player.x);
        object.insert(QStringLiteral("y"), player.y);
        object.insert(QStringLiteral("spawnX"), player.spawnX);
        object.insert(QStringLiteral("spawnY"), player.spawnY);
        object.insert(QStringLiteral("respawnZoneId"), player.respawnZoneId);
        object.insert(QStringLiteral("hp"), player.hp);
        object.insert(QStringLiteral("maxHp"), player.maxHp);
        object.insert(QStringLiteral("coins"), player.coins);
        object.insert(QStringLiteral("stone"), player.stone);
        object.insert(QStringLiteral("aluminum"), player.aluminum);
        object.insert(QStringLiteral("iron"), player.iron);
        object.insert(QStringLiteral("coal"), player.coal);
        object.insert(QStringLiteral("oil"), player.oil);
        object.insert(QStringLiteral("population"), player.population);
        object.insert(QStringLiteral("level"), player.level);
        object.insert(QStringLiteral("kills"), player.kills);
        object.insert(QStringLiteral("experience"), player.experience);
        object.insert(QStringLiteral("respawnAtMs"), QString::number(player.respawnAtMs));
        object.insert(QStringLiteral("invulnerableUntilMs"),
                      QString::number(player.invulnerableUntilMs));
        object.insert(QStringLiteral("color"), player.color.name());
        players.append(object);
    }
    root.insert(QStringLiteral("players"), players);

    QJsonArray zombies;
    for (auto it = m_zombies.constBegin(); it != m_zombies.constEnd(); ++it) {
        const ZombieState& zombie = it.value();
        QJsonObject object;
        object.insert(QStringLiteral("id"), zombie.id);
        object.insert(QStringLiteral("x"), zombie.x);
        object.insert(QStringLiteral("y"), zombie.y);
        object.insert(QStringLiteral("hp"), zombie.hp);
        object.insert(QStringLiteral("maxHp"), zombie.maxHp);
        object.insert(QStringLiteral("kind"), zombie.kind);
        object.insert(QStringLiteral("attackRange"), zombie.attackRange);
        object.insert(QStringLiteral("targetSearchRange"), zombie.targetSearchRange);
        object.insert(QStringLiteral("wasObserved"), zombie.wasObserved);
        object.insert(QStringLiteral("velocityX"), zombie.velocityX);
        object.insert(QStringLiteral("velocityY"), zombie.velocityY);
        object.insert(QStringLiteral("preferredVelocityX"), zombie.preferredVelocityX);
        object.insert(QStringLiteral("preferredVelocityY"), zombie.preferredVelocityY);
        zombies.append(object);
    }
    root.insert(QStringLiteral("zombies"), zombies);

    QJsonArray suspendedZombies;
    for (auto it = m_suspendedZombies.constBegin(); it != m_suspendedZombies.constEnd(); ++it) {
        const ZombieState& zombie = it.value();
        QJsonObject object;
        object.insert(QStringLiteral("id"), zombie.id);
        object.insert(QStringLiteral("x"), zombie.x);
        object.insert(QStringLiteral("y"), zombie.y);
        object.insert(QStringLiteral("hp"), zombie.hp);
        object.insert(QStringLiteral("maxHp"), zombie.maxHp);
        object.insert(QStringLiteral("kind"), zombie.kind);
        object.insert(QStringLiteral("attackRange"), zombie.attackRange);
        object.insert(QStringLiteral("targetSearchRange"), zombie.targetSearchRange);
        object.insert(QStringLiteral("wasObserved"), zombie.wasObserved);
        object.insert(QStringLiteral("velocityX"), zombie.velocityX);
        object.insert(QStringLiteral("velocityY"), zombie.velocityY);
        object.insert(QStringLiteral("preferredVelocityX"), zombie.preferredVelocityX);
        object.insert(QStringLiteral("preferredVelocityY"), zombie.preferredVelocityY);
        suspendedZombies.append(object);
    }
    root.insert(QStringLiteral("suspendedZombies"), suspendedZombies);

    QJsonArray dormantZombies;
    for (int index = 0; index < m_dormantZombieDensity.size(); ++index) {
        const int count = m_dormantZombieDensity.at(index);
        if (count <= 0) {
            continue;
        }
        QJsonObject object;
        object.insert(QStringLiteral("cell"), index);
        object.insert(QStringLiteral("count"), count);
        dormantZombies.append(object);
    }
    root.insert(QStringLiteral("dormantZombies"), dormantZombies);

    QJsonArray zones;
    for (auto it = m_zones.constBegin(); it != m_zones.constEnd(); ++it) {
        const ZoneState& zone = it.value();
        QJsonObject object;
        object.insert(QStringLiteral("id"), zone.id);
        object.insert(QStringLiteral("ownerId"), QString::number(zone.ownerId));
        object.insert(QStringLiteral("color"), zone.color.name());
        object.insert(QStringLiteral("state"), zone.state);
        object.insert(QStringLiteral("shieldLayers"), zone.shieldLayers);
        object.insert(QStringLiteral("claimedAtMs"), QString::number(zone.claimedAtMs));
        object.insert(QStringLiteral("stabilizeAtMs"), QString::number(zone.stabilizeAtMs));
        object.insert(QStringLiteral("reinforceUntilMs"), QString::number(zone.reinforceUntilMs));
        zones.append(object);
    }
    root.insert(QStringLiteral("zones"), zones);

    const QString json = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    const QString escaped = m_database->escapeString(json);
    const QString sql = QStringLiteral(
        "insert into defense_game_world(world_id,state_json,updated_at) values(%1,'%2'::jsonb,current_timestamp) "
        "on conflict(world_id) do update set state_json=excluded.state_json,updated_at=current_timestamp")
        .arg(m_worldId).arg(escaped);
    return m_database->UpdateMySql(sql.toUtf8().constData());
}

void DefenseGameServer::rebuildZoneCells()
{
    for (auto it = m_zones.begin(); it != m_zones.end(); ++it) {
        it->cells.clear();
    }
    for (int index = 0; index < m_cells.size(); ++index) {
        CellState& cell = m_cells[index];
        if (ZoneState* zone = zoneById(cell.zoneId)) {
            zone->cells.insert(index);
        } else {
            cell.zoneId = 0;
        }
    }
}
