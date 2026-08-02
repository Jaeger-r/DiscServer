#ifndef DEFENSEGAMESERVER_H
#define DEFENSEGAMESERVER_H

#include <QColor>
#include <QHash>
#include <QObject>
#include <QPointF>
#include <QSet>
#include <QTimer>
#include <QVector>

#include <functional>

#include "../Packdef.h"
#include "../server/INet.h"

class CMySql;
class DefenseGamePathfindingTest;

class DefenseGameServer : public QObject
{
    Q_OBJECT

public:
    struct PlayerTransferState {
        QString userName;
        float sourceX = 0.0f;
        float sourceY = 0.0f;
        int hp = 100;
        int maxHp = 100;
        int coins = GAME_PLAYER_START_COINS;
        int stone = GAME_PLAYER_START_STONE;
        int aluminum = 0;
        int iron = 0;
        int coal = 0;
        int oil = 0;
        int population = GAME_INITIAL_POPULATION;
        int level = 1;
        int kills = 0;
        int experience = 0;
        QColor color = QColor(38, 166, 154);
        qint64 lastAttackAtMs = 0;
        qint64 respawnAtMs = 0;
        qint64 invulnerableUntilMs = 0;
    };

    using MapTransitionHandler = std::function<bool(
        ConnectionId, qint64, int, int)>;

    explicit DefenseGameServer(QObject* parent = nullptr);

    void setDependencies(CMySql* database, INet* network);
    void setWorldId(int worldId);
    int worldId() const;
    bool hasSessions() const;
    void setMapTransitionHandler(MapTransitionHandler handler);
    bool initialize();
    void shutdown();
    void handleJoin(ConnectionId socket,
                    qint64 authenticatedUserId,
                    const QString& userName,
                    const STRU_GAME_JOIN_RQ& request);
    void handleAction(ConnectionId socket,
                      qint64 authenticatedUserId,
                      const STRU_GAME_ACTION_RQ& request);
    void handleDisconnected(ConnectionId socket);
    bool takePlayerForTransition(ConnectionId socket, qint64 userId,
                                 PlayerTransferState& transfer);
    void acceptPlayerTransition(ConnectionId socket, qint64 userId,
                                const QString& userName,
                                const PlayerTransferState& transfer,
                                int directionX, int directionY);
    bool persistNow();
    void setZombieSpawnDistanceCells(float distanceCells);

private slots:
    void tick();

private:
    friend class DefenseGamePathfindingTest;

    struct CellState {
        int terrain = 0;
        int resourceType = _game_resource_none;
        int resourceAmount = 0;
        int buildingType = _game_building_none;
        qint64 buildingOwnerId = 0;
        int buildingHp = 0;
        int buildingLevel = 0;
        int buildingExp = 0;
        int zoneId = 0;
    };

    struct PlayerState {
        qint64 userId = 0;
        QString userName;
        float x = 0.0f;
        float y = 0.0f;
        float spawnX = 0.0f;
        float spawnY = 0.0f;
        int respawnZoneId = 0;
        float moveX = 0.0f;
        float moveY = 0.0f;
        float targetX = 0.0f;
        float targetY = 0.0f;
        bool hasMoveTarget = false;
        int hp = 100;
        int maxHp = 100;
        int coins = GAME_PLAYER_START_COINS;
        int stone = GAME_PLAYER_START_STONE;
        int aluminum = 0;
        int iron = 0;
        int coal = 0;
        int oil = 0;
        int population = GAME_INITIAL_POPULATION;
        int level = 1;
        int kills = 0;
        int experience = 0;
        QColor color = QColor(38, 166, 154);
        bool online = false;
        qint64 lastAttackAtMs = 0;
        qint64 lastTerritoryRegenAtMs = 0;
        qint64 respawnAtMs = 0;
        qint64 invulnerableUntilMs = 0;
    };

    struct ZombieState {
        int id = 0;
        float x = 0.0f;
        float y = 0.0f;
        int hp = 50;
        int maxHp = 50;
        int kind = 0;
        qint64 lastAttackAtMs = 0;
        QVector<QPoint> path;
        int pathIndex = 0;
        qint64 targetUserId = 0;
        int targetBuildingIndex = -1;
        QPoint targetCell = QPoint(-1, -1);
        float attackRange = 0.72f;
        float targetSearchRange = 18.0f;
        qint64 pathUpdatedAtMs = 0;
        bool pathUsesBreaches = false;
        QSet<int> plannedBreaches;
        float plannedPathCost = 0.0f;
        qint64 lastProgressAtMs = 0;
        float lastProgressX = 0.0f;
        float lastProgressY = 0.0f;
        qint64 lastAiUpdateAtMs = 0;
        qint64 lastDamageOwnerId = 0;
        int lastDamageBuildingIndex = -1;
        bool lastDamageWasPlayer = false;
        bool wasObserved = false;
        float velocityX = 0.0f;
        float velocityY = 0.0f;
        float preferredVelocityX = 0.0f;
        float preferredVelocityY = 0.0f;
    };

    struct ZombiePathResult {
        QVector<QPoint> cells;
        qint64 targetUserId = 0;
        float cost = 0.0f;
        bool usesBreaches = false;
        QSet<int> breachCells;
    };

    struct ZoneState {
        int id = 0;
        qint64 ownerId = 0;
        QColor color = QColor(38, 166, 154);
        int state = _game_zone_stabilizing;
        int shieldLayers = 0;
        qint64 claimedAtMs = 0;
        qint64 stabilizeAtMs = 0;
        qint64 reinforceUntilMs = 0;
        QSet<int> cells;
    };

    struct BuildingCost {
        int coins = 0;
        int stone = 0;
        int aluminum = 0;
        int iron = 0;
        int coal = 0;
        int oil = 0;
        bool valid = false;
    };

    struct RepairState {
        qint64 ownerId = 0;
        qint64 lastTickAtMs = 0;
    };

    struct PopulationSupport {
        int capacity = GAME_BASE_POPULATION_CAPACITY;
        int food = GAME_BASE_FOOD_CAPACITY;
        int water = GAME_BASE_WATER_CAPACITY;
    };

    int cellIndex(int x, int y) const;
    bool inBounds(int x, int y) const;
    bool isCellOccupiedByUnit(int x, int y) const;
    bool isCellWalkable(int x, int y) const;
    bool isPositionBlocked(float x, float y, float radius) const;
    QPointF findSpawnPosition() const;
    QPointF findSafePositionNear(float x, float y) const;
    QPointF findSpawnPositionInZone(const ZoneState& zone) const;
    QPointF preferredPlayerSpawn(qint64 userId, int preferredZoneId,
                                 int* selectedZoneId = nullptr) const;
    bool userStillPresent(qint64 userId) const;
    PlayerState* playerForUser(qint64 userId);
    const PlayerState* playerForUser(qint64 userId) const;
    ZoneState* zoneById(int zoneId);
    const ZoneState* zoneById(int zoneId) const;
    ZoneState* adjacentZoneForBuilding(int x, int y);
    const ZoneState* protectingZoneForCell(int x, int y) const;
    bool isReinforcedZoneBarrier(int x, int y, qint64 nowMs) const;

    void initializeMap();
    void generateMapFeatures();
    void processMove(PlayerState& player, float directionX, float directionY);
    void processMoveTarget(PlayerState& player, float targetX, float targetY);
    void processAttack(PlayerState& player, float targetX, float targetY, qint64 nowMs);
    void processBuild(PlayerState& player, int x, int y, int buildingType);
    void processDemolish(PlayerState& player, int x, int y);
    void processUpgrade(PlayerState& player, int x, int y);
    void processRepair(PlayerState& player, int x, int y);
    void processSetRespawnZone(PlayerState& player, int x, int y);
    void refreshPlayerRespawnLocation(PlayerState& player);
    void processRepairs(qint64 nowMs);
    void detectNewZones(qint64 wallOwnerId, qint64 nowMs);
    void validateStabilizingZones();
    bool isZoneFullyEnclosed(const ZoneState& zone) const;
    void promoteStableZones(qint64 nowMs);
    void processPlayerMovement(float deltaSeconds);
    void processPlayerAutoAttacks(qint64 nowMs);
    void processPlayerRespawns(qint64 nowMs);
    void processPlayerRegeneration(qint64 nowMs);
    void removePlayerAssets(qint64 userId);
    void processTurrets(qint64 nowMs);
    void applyTurretSplashDamage(const CellState& turret, int turretIndex,
                                 int primaryZombieId, float impactX, float impactY);
    void processExtractors(qint64 nowMs);
    void processPopulation(qint64 nowMs);
    void processZombies(qint64 nowMs, float deltaSeconds);
    void applyZombieCrowdAvoidance(qint64 nowMs, float deltaSeconds);
    void moveZombieWithFlowField(ZombieState& zombie, qint64 nowMs, float deltaSeconds);
    void resolveZombieOverlaps();
    bool isZombiePositionBlocked(float x, float y, float radius) const;
    void moveZombie(ZombieState& zombie, qint64 nowMs, float deltaSeconds,
                    bool mayRebuildPath = true);
    bool rebuildZombiePath(ZombieState& zombie, qint64 nowMs);
    ZombiePathResult findZombiePath(const QPoint& start,
                                    const QHash<int, qint64>& goals,
                                    bool allowBreaching) const;
    float zombieTraversalCost(int x, int y, bool allowBreaching, qint64 nowMs = 0) const;
    bool zombiePathNeedsUpdate(const ZombieState& zombie, qint64 nowMs) const;
    void damageZombie(ZombieState& zombie, int damage, qint64 ownerId,
                      int buildingIndex, bool playerDamage);
    void explodeGiantZombie(const ZombieState& zombie, qint64 nowMs);
    int zombieAttack(int kind) const;
    float zombieCollisionRadius(int kind) const;
    float zombieSpeedMultiplier(int kind) const;
    int buildingMaxHp(int buildingType, int level) const;
    int buildingExpRequired(int level) const;
    int buildingUpgradeCost(int level) const;
    int buildingRepairCost(const CellState& building) const;
    BuildingCost buildingCost(int buildingType) const;
    bool canAfford(const PlayerState& player, const BuildingCost& cost) const;
    void applyCost(PlayerState& player, const BuildingCost& cost, int multiplier = -1);
    QString formatCost(const BuildingCost& cost) const;
    bool isExtractor(int buildingType) const;
    bool isProductionBuilding(int buildingType) const;
    bool isPopulationBuilding(int buildingType) const;
    bool isTurretBuilding(int buildingType) const;
    int turretPopulationUsed(qint64 userId) const;
    int waveShieldAt(float x, float y) const;
    int housePopulationCapacity(int level) const;
    PopulationSupport populationSupport(qint64 userId) const;
    int extractorResourceType(int buildingType) const;
    int extractorResourceCell(int buildingIndex, int buildingType) const;
    int playerAttack(const PlayerState& player) const;
    float playerAttackRange(const PlayerState& player) const;
    qint64 playerAttackIntervalMs(const PlayerState& player) const;
    int playerExperienceRequired(int level) const;
    int turretAttack(int buildingType, int level) const;
    float turretRange(int buildingType, int level) const;
    qint64 turretFireIntervalMs(int buildingType) const;
    void damageBuilding(ZombieState& zombie, int x, int y, int damage, qint64 nowMs);
    void breachZoneCell(int x, int y);
    void damagePlayer(PlayerState& player, int damage, qint64 nowMs);
    void removeDeadZombies();
    void resetWaveIfNoLivingPlayers(qint64 nowMs);
    void resetWaveProgress(qint64 nowMs);
    void processRandomDisasters(qint64 nowMs);
    void scheduleNextDisaster(qint64 nowMs);
    int triggerZombieOutbreak(const QPointF& center, int zombieCount);
    void triggerLightningStrike(const QPointF& center, qint64 nowMs);
    QPointF randomLightningStrikePoint() const;
    void spawnWave(qint64 nowMs);
    void spawnZombie(int hp, int kind);
    void spawnZombieAt(const QPointF& position, int hp, int kind);
    ZombieState acquireZombieState();
    void recycleZombieState(ZombieState&& zombie);
    void initializeDormantHordes();
    void replenishDormantHordes(qint64 nowMs);
    void advanceDormantHordes(qint64 nowMs);
    void materializeNearbyHordes(qint64 nowMs, bool force = false);
    void dematerializeDistantZombies(qint64 nowMs);
    bool zombieIsVisibleToAnyPlayer(const ZombieState& zombie) const;
    QVector<bool> zombieSpawnAllowedMask() const;
    void rebuildFlowField(qint64 nowMs);
    void rebuildSectorFlowField(qint64 nowMs);
    int sectorIndexForCell(int x, int y) const;
    float distanceToNearestPlayerForce(float x, float y) const;
    void rebuildZombieSpatialIndex();
    QVector<int> nearbyZombieIds(float x, float y, float radius) const;
    int spatialBucketIndex(float x, float y) const;
    QPointF zombieSpawnPoint() const;
    QVector<QPointF> safeBoundaryPoints() const;

    void sendSnapshot(ConnectionId socket);
    void broadcastSnapshot();
    void sendEvent(qint64 userId, int eventType, const QString& message,
                   float x = -1.0f, float y = -1.0f);
    STRU_GAME_STATE_RS makeSnapshot() const;

    bool loadWorld();
    bool saveWorld();
    void rebuildZoneCells();

    CMySql* m_database = nullptr;
    INet* m_network = nullptr;
    QVector<CellState> m_cells;
    QHash<qint64, PlayerState> m_players;
    QHash<int, ZombieState> m_zombies;
    QHash<int, ZombieState> m_suspendedZombies;
    QVector<ZombieState> m_zombiePool;
    QVector<int> m_dormantZombieDensity;
    QVector<float> m_flowCosts;
    QVector<QPoint> m_flowNextCells;
    QVector<qint64> m_flowTargetIds;
    QVector<std::uint8_t> m_flowTargetKinds;
    QVector<float> m_sectorFlowCosts;
    QVector<int> m_sectorFlowNext;
    QVector<QVector<int>> m_zombieSpatialBuckets;
    QHash<int, ZoneState> m_zones;
    QHash<int, qint64> m_turretLastAttackAtMs;
    QHash<int, RepairState> m_repairs;
    QHash<ConnectionId, qint64> m_sessions;
    QTimer m_tickTimer;
    int m_nextZombieId = 1;
    int m_nextZoneId = 1;
    int m_wave = 0;
    qint64 m_nextWaveAtMs = 0;
    qint64 m_lastSimulationAtMs = 0;
    qint64 m_lastCombatAtMs = 0;
    qint64 m_lastExtractionAtMs = 0;
    qint64 m_lastPopulationAtMs = 0;
    qint64 m_lastBroadcastAtMs = 0;
    qint64 m_lastPersistAtMs = 0;
    qint64 m_lastFlowFieldAtMs = 0;
    qint64 m_lastDormantReplenishAtMs = 0;
    qint64 m_lastDormantAdvanceAtMs = 0;
    qint64 m_lastDormantMaterializeAtMs = 0;
    qint64 m_lastOverlapCorrectionAtMs = 0;
    qint64 m_lastCrowdAvoidanceAtMs = 0;
    qint64 m_waveAssaultUntilMs = 0;
    qint64 m_nextDisasterAtMs = 0;
    bool m_waveResetForNoSurvivors = false;
    int m_aiSchedulerCursor = 0;
    int m_worldId = 1;
    float m_zombieSpawnDistanceCells = 10.0f;
    float m_playerSpeedCellsPerSecond = 3.2f;
    float m_zombieSpeedCellsPerSecond = 0.55f;
    MapTransitionHandler m_mapTransitionHandler;
};

#endif // DEFENSEGAMESERVER_H
