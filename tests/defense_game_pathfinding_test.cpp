#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>

#include "game/DefenseGameServer.h"

class DefenseGamePathfindingTest
{
public:
    static bool avoidsWallWhenOpenRouteExists()
    {
        DefenseGameServer server;
        server.initializeMap();

        DefenseGameServer::PlayerState player;
        player.userId = 1;
        player.online = true;
        player.x = 6.5f;
        player.y = 4.5f;
        server.m_players.insert(player.userId, player);

        DefenseGameServer::CellState& wall = server.m_cells[server.cellIndex(3, 4)];
        wall.buildingType = _game_building_wall;
        wall.buildingOwnerId = 0;
        wall.buildingHp = GAME_WALL_MAX_HP;

        DefenseGameServer::ZombieState zombie;
        zombie.id = 1;
        zombie.x = 1.5f;
        zombie.y = 4.5f;
        if (!server.rebuildZombiePath(zombie, 1000) || zombie.pathUsesBreaches) {
            return false;
        }
        return !zombie.path.contains(QPoint(3, 4)) && zombie.targetUserId == player.userId;
    }

    static bool breachesOnlyWhenNoOpenRouteExists()
    {
        DefenseGameServer server;
        server.initializeMap();

        DefenseGameServer::PlayerState player;
        player.userId = 2;
        player.online = true;
        player.x = 7.5f;
        player.y = 8.5f;
        server.m_players.insert(player.userId, player);

        for (int y = 0; y < GAME_MAP_HEIGHT; ++y) {
            DefenseGameServer::CellState& wall = server.m_cells[server.cellIndex(5, y)];
            wall.buildingType = _game_building_wall;
            wall.buildingOwnerId = 0;
            wall.buildingHp = GAME_WALL_MAX_HP;
        }
        server.m_cells[server.cellIndex(5, 6)].buildingHp = 10;

        DefenseGameServer::ZombieState zombie;
        zombie.id = 2;
        zombie.x = 2.5f;
        zombie.y = 8.5f;
        if (!server.rebuildZombiePath(zombie, 1000) || !zombie.pathUsesBreaches) {
            return false;
        }
        return zombie.path.contains(QPoint(5, 6));
    }

    static bool breachesWhenOpenDetourIsFarMoreExpensive()
    {
        DefenseGameServer server;
        server.initializeMap();

        DefenseGameServer::PlayerState player;
        player.userId = 22;
        player.online = true;
        player.x = 28.5f;
        player.y = 50.5f;
        server.m_players.insert(player.userId, player);

        for (int y = 1; y < GAME_MAP_HEIGHT - 1; ++y) {
            DefenseGameServer::CellState& wall = server.m_cells[server.cellIndex(20, y)];
            wall.buildingType = _game_building_wall;
            wall.buildingOwnerId = 0;
            wall.buildingHp = GAME_WALL_MAX_HP;
        }

        DefenseGameServer::ZombieState zombie;
        zombie.id = 22;
        zombie.x = 10.5f;
        zombie.y = 50.5f;
        if (!server.rebuildZombiePath(zombie, 1000) || !zombie.pathUsesBreaches) {
            return false;
        }
        return zombie.plannedBreaches.size() == 1
            && zombie.path.contains(QPoint(20, 50));
    }

    static bool eachZombieUsesItsOwnTargetRange()
    {
        DefenseGameServer server;
        server.initializeMap();

        DefenseGameServer::PlayerState player;
        player.userId = 26;
        player.online = true;
        player.x = 40.5f;
        player.y = 10.5f;
        server.m_players.insert(player.userId, player);

        DefenseGameServer::ZombieState shortRangeZombie;
        shortRangeZombie.id = 26;
        shortRangeZombie.x = 10.5f;
        shortRangeZombie.y = 10.5f;
        shortRangeZombie.targetSearchRange = 8.0f;
        if (server.rebuildZombiePath(shortRangeZombie, 1000)) {
            return false;
        }

        DefenseGameServer::ZombieState longRangeZombie = shortRangeZombie;
        longRangeZombie.id = 27;
        longRangeZombie.targetSearchRange = 35.0f;
        return server.rebuildZombiePath(longRangeZombie, 1000)
            && longRangeZombie.targetUserId == player.userId
            && longRangeZombie.targetBuildingIndex < 0;
    }

    static bool zombieTargetPriorityIsPlayerExtractorThenOffensiveBuilding()
    {
        DefenseGameServer server;
        server.initializeMap();

        DefenseGameServer::PlayerState player;
        player.userId = 27;
        player.online = true;
        player.x = 12.5f;
        player.y = 10.5f;
        server.m_players.insert(player.userId, player);

        DefenseGameServer::CellState& wall = server.m_cells[server.cellIndex(11, 10)];
        wall.buildingType = _game_building_wall;
        wall.buildingOwnerId = player.userId;
        wall.buildingHp = GAME_WALL_MAX_HP;
        DefenseGameServer::CellState& turret = server.m_cells[server.cellIndex(10, 11)];
        turret.buildingType = _game_building_turret;
        turret.buildingOwnerId = player.userId;
        turret.buildingHp = GAME_TURRET_MAX_HP;
        DefenseGameServer::CellState& extractor = server.m_cells[server.cellIndex(10, 9)];
        extractor.buildingType = _game_building_stone_extractor;
        extractor.buildingOwnerId = player.userId;
        extractor.buildingHp = GAME_EXTRACTOR_MAX_HP;

        DefenseGameServer::ZombieState zombie;
        zombie.id = 28;
        zombie.x = 10.5f;
        zombie.y = 10.5f;
        zombie.attackRange = 0.72f;
        zombie.targetSearchRange = 8.0f;
        server.rebuildFlowField(1000);
        const int zombieCell = server.cellIndex(10, 10);
        if (server.m_flowTargetKinds.value(zombieCell) != 2
            || server.m_flowTargetIds.value(zombieCell) != player.userId) {
            qCritical() << "Player flow priority failed"
                        << server.m_flowTargetKinds.value(zombieCell)
                        << server.m_flowTargetIds.value(zombieCell);
            return false;
        }
        if (!server.rebuildZombiePath(zombie, 1000)
            || zombie.targetUserId != player.userId || zombie.targetBuildingIndex >= 0) {
            qCritical() << "Player local priority failed" << zombie.targetUserId
                        << zombie.targetBuildingIndex << zombie.path.size();
            return false;
        }

        server.m_players[player.userId].respawnAtMs = 5000;
        server.rebuildFlowField(2000);
        if (server.m_flowTargetKinds.value(zombieCell) != 1
            || server.m_flowTargetIds.value(zombieCell) != server.cellIndex(10, 9)) {
            qCritical() << "Extractor flow priority failed"
                        << server.m_flowTargetKinds.value(zombieCell)
                        << server.m_flowTargetIds.value(zombieCell)
                        << server.cellIndex(10, 9);
            return false;
        }
        if (!server.rebuildZombiePath(zombie, 2000)
            || zombie.targetBuildingIndex != server.cellIndex(10, 9)) {
            qCritical() << "Extractor local priority failed"
                        << zombie.targetBuildingIndex << zombie.path.size();
            return false;
        }
        extractor = DefenseGameServer::CellState();
        if (!server.rebuildZombiePath(zombie, 3000)
            || zombie.targetBuildingIndex != server.cellIndex(10, 11)) {
            qCritical() << "Turret local priority failed"
                        << zombie.targetBuildingIndex << zombie.path.size();
            return false;
        }
        turret = DefenseGameServer::CellState();
        return !server.rebuildZombiePath(zombie, 4000)
            && wall.buildingHp == GAME_WALL_MAX_HP;
    }

    static bool smallTargetMovementKeepsCurrentPath()
    {
        DefenseGameServer server;
        server.initializeMap();

        DefenseGameServer::PlayerState player;
        player.userId = 20;
        player.online = true;
        player.x = 14.5f;
        player.y = 14.5f;
        server.m_players.insert(player.userId, player);

        DefenseGameServer::ZombieState zombie;
        zombie.id = 20;
        zombie.x = 2.5f;
        zombie.y = 14.5f;
        if (!server.rebuildZombiePath(zombie, 1000)) {
            return false;
        }
        server.m_players[player.userId].x += 1.0f;
        return !server.zombiePathNeedsUpdate(zombie, 1500)
            && server.zombiePathNeedsUpdate(zombie, 3200);
    }

    static bool movingTargetTwoCellsInvalidatesPath()
    {
        DefenseGameServer server;
        server.initializeMap();

        DefenseGameServer::PlayerState player;
        player.userId = 23;
        player.online = true;
        player.x = 14.5f;
        player.y = 14.5f;
        server.m_players.insert(player.userId, player);

        DefenseGameServer::ZombieState zombie;
        zombie.id = 23;
        zombie.x = 2.5f;
        zombie.y = 14.5f;
        if (!server.rebuildZombiePath(zombie, 1000)) {
            return false;
        }
        server.m_players[player.userId].x += 2.0f;
        return server.zombiePathNeedsUpdate(zombie, 1100);
    }

    static bool mountainsRemainImpassable()
    {
        DefenseGameServer server;
        server.initializeMap();

        DefenseGameServer::PlayerState player;
        player.userId = 24;
        player.online = true;
        player.x = 70.5f;
        player.y = 50.5f;
        server.m_players.insert(player.userId, player);
        for (int y = 0; y < GAME_MAP_HEIGHT; ++y) {
            server.m_cells[server.cellIndex(50, y)].terrain = _game_terrain_mountain;
        }

        DefenseGameServer::ZombieState zombie;
        zombie.id = 24;
        zombie.x = 45.5f;
        zombie.y = 50.5f;
        zombie.targetSearchRange = 30.0f;
        return !server.rebuildZombiePath(zombie, 1000) && zombie.path.isEmpty();
    }

    static bool overlappingZombiesKeepForwardProgress()
    {
        DefenseGameServer server;
        server.initializeMap();

        DefenseGameServer::PlayerState player;
        player.userId = 25;
        player.online = true;
        player.x = 20.5f;
        player.y = 20.5f;
        server.m_players.insert(player.userId, player);

        for (int id = 1; id <= 4; ++id) {
            DefenseGameServer::ZombieState zombie;
            zombie.id = id;
            zombie.x = 10.5f;
            zombie.y = 20.5f;
            if (!server.rebuildZombiePath(zombie, 1000)) {
                return false;
            }
            server.m_zombies.insert(id, zombie);
        }
        for (int tick = 1; tick <= 30; ++tick) {
            const qint64 nowMs = 1000 + tick * 50;
            for (int id = 1; id <= 4; ++id) {
                server.moveZombie(server.m_zombies[id], nowMs, 0.05f, true);
            }
        }
        for (int id = 1; id <= 4; ++id) {
            if (server.m_zombies[id].x < 11.1f) {
                return false;
            }
        }
        return true;
    }

    static bool flowFieldSchedulerAvoidsIndividualAStarPaths()
    {
        DefenseGameServer server;
        server.initializeMap();

        DefenseGameServer::PlayerState player;
        player.userId = 21;
        player.online = true;
        player.x = 50.5f;
        player.y = 50.5f;
        server.m_players.insert(player.userId, player);
        server.rebuildFlowField(1000);
        server.m_waveAssaultUntilMs = 5000;

        for (int i = 0; i < 12; ++i) {
            DefenseGameServer::ZombieState zombie;
            zombie.id = 100 + i;
            zombie.x = 5.5f + i;
            zombie.y = 5.5f;
            server.m_zombies.insert(zombie.id, zombie);
        }
        server.processZombies(1000, 0.05f);

        int updated = 0;
        for (auto zombie = server.m_zombies.constBegin(); zombie != server.m_zombies.constEnd(); ++zombie) {
            updated += zombie->lastAiUpdateAtMs == 1000 && zombie->path.isEmpty() ? 1 : 0;
        }
        return updated == 12;
    }

    static bool zoneBufferDormantHordesSpatialIndexAndPoolWork()
    {
        DefenseGameServer server;
        server.initializeMap();

        DefenseGameServer::ZoneState zone;
        zone.id = 30;
        zone.ownerId = 30;
        zone.state = _game_zone_permanent;
        for (int y = 10; y <= 14; ++y) {
            for (int x = 10; x <= 14; ++x) {
                const int index = server.cellIndex(x, y);
                zone.cells.insert(index);
                server.m_cells[index].zoneId = zone.id;
            }
        }
        server.m_zones.insert(zone.id, zone);
        const QVector<bool> allowed = server.zombieSpawnAllowedMask();
        if (allowed.at(server.cellIndex(5, 12))
            || !allowed.at(server.cellIndex(4, 12))) {
            return false;
        }
        server.initializeDormantHordes();
        int dormantPopulation = 0;
        int allowedCellCount = 0;
        for (int index = 0; index < server.m_dormantZombieDensity.size(); ++index) {
            dormantPopulation += server.m_dormantZombieDensity.at(index);
            allowedCellCount += allowed.at(index) ? 1 : 0;
            if (server.m_dormantZombieDensity.at(index) > 0 && !allowed.at(index)) {
                return false;
            }
        }
        if (dormantPopulation < allowedCellCount * 2
            || dormantPopulation > allowedCellCount * 4) {
            return false;
        }
        int vacancyIndex = -1;
        for (int index = 0; index < allowed.size(); ++index) {
            if (allowed.at(index)) {
                vacancyIndex = index;
                break;
            }
        }
        const int forbiddenIndex = server.cellIndex(5, 12);
        for (int index = 0; index < allowed.size(); ++index) {
            server.m_dormantZombieDensity[index] = allowed.at(index) ? 3 : 0;
        }
        server.m_dormantZombieDensity[vacancyIndex] = 0;
        server.m_dormantZombieDensity[forbiddenIndex] = 1;
        server.m_lastDormantReplenishAtMs = 0;
        server.replenishDormantHordes(6000);
        if (server.m_dormantZombieDensity.at(vacancyIndex) != 1
            || server.m_dormantZombieDensity.at(forbiddenIndex) != 0) {
            return false;
        }

        for (int index = 0; index < allowed.size(); ++index) {
            server.m_dormantZombieDensity[index] = allowed.at(index) ? 4 : 0;
        }
        int before = 0;
        for (int count : server.m_dormantZombieDensity) {
            before += count;
        }
        server.m_lastDormantReplenishAtMs = 0;
        server.replenishDormantHordes(12000);
        int after = 0;
        for (int count : server.m_dormantZombieDensity) {
            after += count;
        }
        if (before != after) {
            return false;
        }

        DefenseGameServer::ZombieState nearZombie;
        nearZombie.id = 301;
        nearZombie.x = 20.5f;
        nearZombie.y = 20.5f;
        server.m_zombies.insert(nearZombie.id, nearZombie);
        DefenseGameServer::ZombieState farZombie = nearZombie;
        farZombie.id = 302;
        farZombie.x = 60.5f;
        server.m_zombies.insert(farZombie.id, farZombie);
        server.rebuildZombieSpatialIndex();
        const QVector<int> nearby = server.nearbyZombieIds(20.5f, 20.5f, 3.0f);
        if (!nearby.contains(nearZombie.id) || nearby.contains(farZombie.id)) {
            return false;
        }

        server.m_zombies[nearZombie.id].hp = 0;
        server.removeDeadZombies();
        if (server.m_zombiePool.size() != 1) {
            return false;
        }
        server.spawnZombieAt(QPointF(22.5f, 22.5f), 50, 0);
        return server.m_zombiePool.isEmpty();
    }

    static bool dormantMaterializationUsesClearanceAndCadence()
    {
        DefenseGameServer server;
        server.initializeMap();
        DefenseGameServer::PlayerState player;
        player.userId = 90;
        player.online = true;
        player.x = 50.5f;
        player.y = 50.5f;
        server.m_players.insert(player.userId, player);
        server.initializeDormantHordes();
        server.rebuildFlowField(1000);

        server.materializeNearbyHordes(1000);
        if (server.m_zombies.size() != 4) {
            return false;
        }
        for (auto zombie = server.m_zombies.constBegin(); zombie != server.m_zombies.constEnd(); ++zombie) {
            if (server.distanceToNearestPlayerForce(zombie->x, zombie->y) < 5.0f) {
                return false;
            }
        }
        server.materializeNearbyHordes(1200);
        if (server.m_zombies.size() != 4) {
            return false;
        }
        server.materializeNearbyHordes(1800);
        return server.m_zombies.size() == 8;
    }

    static bool distantZombiesSuspendAndRestoreWithoutLosingIdentity()
    {
        DefenseGameServer server;
        server.initializeMap();
        server.initializeDormantHordes();
        const int index = server.cellIndex(24, 24);
        server.m_dormantZombieDensity[index] = 0;
        DefenseGameServer::ZombieState zombie;
        zombie.id = 501;
        zombie.x = 24.5f;
        zombie.y = 24.5f;
        zombie.hp = 37;
        zombie.maxHp = 80;
        zombie.kind = GAME_ZOMBIE_KIND_ELITE;
        server.m_zombies.insert(zombie.id, zombie);
        server.rebuildFlowField(1000);
        server.dematerializeDistantZombies(1000);
        if (!server.m_zombies.isEmpty()
            || !server.m_suspendedZombies.contains(zombie.id)
            || server.m_dormantZombieDensity.at(index) != 0
            || !server.m_zombiePool.isEmpty()) {
            return false;
        }

        DefenseGameServer::PlayerState player;
        player.userId = 90;
        player.online = true;
        player.x = 24.5f;
        player.y = 24.5f;
        server.m_players.insert(player.userId, player);
        server.rebuildFlowField(2000);
        server.m_lastDormantMaterializeAtMs = 0;
        server.materializeNearbyHordes(2000);
        return server.m_suspendedZombies.isEmpty()
            && server.m_zombies.contains(zombie.id)
            && server.m_zombies.value(zombie.id).hp == 37
            && server.m_zombies.value(zombie.id).maxHp == 80
            && server.m_zombies.value(zombie.id).kind == GAME_ZOMBIE_KIND_ELITE;
    }

    static bool zombiesInsidePlayerVisionNeverSuspend()
    {
        DefenseGameServer server;
        server.initializeMap();
        server.initializeDormantHordes();

        DefenseGameServer::PlayerState player;
        player.userId = 91;
        player.online = true;
        player.x = 90.5f;
        player.y = 90.5f;
        server.m_players.insert(player.userId, player);

        DefenseGameServer::ZoneState zone;
        zone.id = 91;
        zone.ownerId = player.userId;
        zone.state = _game_zone_stabilizing;
        const int zoneCell = server.cellIndex(8, 8);
        zone.cells.insert(zoneCell);
        server.m_cells[zoneCell].zoneId = zone.id;
        server.m_zones.insert(zone.id, zone);

        DefenseGameServer::ZombieState zombie;
        zombie.id = 502;
        zombie.x = 8.5f;
        zombie.y = 8.5f;
        server.m_zombies.insert(zombie.id, zombie);
        server.rebuildFlowField(1000);
        server.dematerializeDistantZombies(1000);
        return server.m_zombies.contains(zombie.id)
            && !server.m_suspendedZombies.contains(zombie.id)
            && server.m_zombies.value(zombie.id).wasObserved;
    }

    static bool observedZombieUsesLocalAggroOutsideGlobalSimulationRange()
    {
        DefenseGameServer server;
        server.initializeMap();

        DefenseGameServer::PlayerState distantPlayer;
        distantPlayer.userId = 92;
        distantPlayer.online = true;
        distantPlayer.x = 90.5f;
        distantPlayer.y = 90.5f;
        server.m_players.insert(distantPlayer.userId, distantPlayer);

        const int turretIndex = server.cellIndex(11, 10);
        server.m_cells[turretIndex].buildingType = _game_building_turret;
        server.m_cells[turretIndex].buildingOwnerId = 777;
        server.m_cells[turretIndex].buildingHp = GAME_TURRET_MAX_HP;

        DefenseGameServer::ZombieState zombie;
        zombie.id = 503;
        zombie.x = 10.5f;
        zombie.y = 10.5f;
        zombie.wasObserved = true;
        server.m_zombies.insert(zombie.id, zombie);
        server.rebuildFlowField(1000);
        if (server.distanceToNearestPlayerForce(zombie.x, zombie.y)
            <= 28.0f) {
            return false;
        }

        server.processZombies(1000, 0.05f);
        return server.m_cells[turretIndex].buildingHp < GAME_TURRET_MAX_HP
            && server.m_zombies.value(zombie.id).lastAiUpdateAtMs == 1000;
    }

    static bool sectorFlowGuidesDistantHordesTowardTargetSector()
    {
        DefenseGameServer server;
        server.initializeMap();
        DefenseGameServer::PlayerState player;
        player.userId = 93;
        player.online = true;
        player.x = 85.5f;
        player.y = 85.5f;
        server.m_players.insert(player.userId, player);
        server.rebuildFlowField(1000);

        const int origin = server.sectorIndexForCell(5, 5);
        const int next = server.m_sectorFlowNext.value(origin, -1);
        if (next < 0 || next == origin) {
            return false;
        }
        const int originX = origin % 10;
        const int originY = origin / 10;
        const int nextX = next % 10;
        const int nextY = next / 10;
        const STRU_GAME_STATE_RS snapshot = server.makeSnapshot();
        const GameCellInfo& flowCell = snapshot.m_cells[server.cellIndex(5, 5)];
        return qAbs(nextX - originX) + qAbs(nextY - originY) == 1
            && server.m_sectorFlowCosts.value(next)
                < server.m_sectorFlowCosts.value(origin)
            && (flowCell.m_dormantFlowX != 0 || flowCell.m_dormantFlowY != 0)
            && qAbs(flowCell.m_dormantFlowX) <= 1
            && qAbs(flowCell.m_dormantFlowY) <= 1;
    }

    static bool rvoCrowdAvoidancePreventsCrossingAndWallEntry()
    {
        DefenseGameServer crowdServer;
        crowdServer.initializeMap();
        for (int id = 601; id <= 602; ++id) {
            DefenseGameServer::ZombieState zombie;
            zombie.id = id;
            zombie.x = id == 601 ? 10.2f : 10.8f;
            zombie.y = 10.5f;
            zombie.preferredVelocityX = id == 601 ? 0.55f : -0.55f;
            crowdServer.m_zombies.insert(id, zombie);
        }
        crowdServer.applyZombieCrowdAvoidance(50, 0.05f);
        const auto& first = crowdServer.m_zombies.value(601);
        const auto& second = crowdServer.m_zombies.value(602);
        if (!qIsFinite(first.x) || !qIsFinite(first.y)
            || !qIsFinite(second.x) || !qIsFinite(second.y)
            || first.x >= second.x) {
            return false;
        }

        DefenseGameServer wallServer;
        wallServer.initializeMap();
        const int wallIndex = wallServer.cellIndex(11, 10);
        wallServer.m_cells[wallIndex].buildingType = _game_building_wall;
        wallServer.m_cells[wallIndex].buildingHp = GAME_WALL_MAX_HP;
        DefenseGameServer::ZombieState zombie;
        zombie.id = 603;
        zombie.x = 10.79f;
        zombie.y = 10.5f;
        zombie.preferredVelocityX = 0.55f;
        wallServer.m_zombies.insert(zombie.id, zombie);
        wallServer.applyZombieCrowdAvoidance(50, 0.05f);
        return wallServer.m_zombies.value(zombie.id).x
            <= 11.0f - wallServer.zombieCollisionRadius(zombie.kind) + 0.001f;
    }

    static bool rvoCrowdAvoidanceHandlesActiveZombieLimit()
    {
        DefenseGameServer server;
        server.initializeMap();
        for (int index = 0; index < GAME_MAX_ZOMBIES; ++index) {
            DefenseGameServer::ZombieState zombie;
            zombie.id = 700 + index;
            zombie.x = 5.5f + (index % 24) * 0.42f;
            zombie.y = 5.5f + (index / 24) * 0.42f;
            zombie.preferredVelocityX = index % 2 == 0 ? 0.55f : -0.55f;
            server.m_zombies.insert(zombie.id, zombie);
        }
        for (int step = 0; step < 20; ++step) {
            server.applyZombieCrowdAvoidance((step + 1) * 50, 0.05f);
        }
        if (server.m_zombies.size() != GAME_MAX_ZOMBIES) {
            return false;
        }
        for (auto zombie = server.m_zombies.constBegin();
             zombie != server.m_zombies.constEnd(); ++zombie) {
            if (!qIsFinite(zombie->x) || !qIsFinite(zombie->y)
                || zombie->x <= 0.0f || zombie->x >= GAME_MAP_WIDTH
                || zombie->y <= 0.0f || zombie->y >= GAME_MAP_HEIGHT) {
                return false;
            }
        }
        return true;
    }

    static bool collisionCorrectionAndBlockedWallAttackWork()
    {
        DefenseGameServer server;
        server.initializeMap();

        for (int id = 401; id <= 403; ++id) {
            DefenseGameServer::ZombieState zombie;
            zombie.id = id;
            zombie.x = 20.5f;
            zombie.y = 20.5f;
            server.m_zombies.insert(id, zombie);
        }
        server.resolveZombieOverlaps();
        for (int firstId = 401; firstId <= 403; ++firstId) {
            for (int secondId = firstId + 1; secondId <= 403; ++secondId) {
                const auto& first = server.m_zombies[firstId];
                const auto& second = server.m_zombies[secondId];
                const float dx = first.x - second.x;
                const float dy = first.y - second.y;
                const float distance = qSqrt(dx * dx + dy * dy);
                const float minimumDistance = server.zombieCollisionRadius(first.kind)
                    + server.zombieCollisionRadius(second.kind);
                if (distance + 0.001f < minimumDistance) {
                    qCritical() << "Zombie pair still overlaps" << firstId << secondId
                                << distance;
                    return false;
                }
            }
        }

        DefenseGameServer::CellState& wall = server.m_cells[server.cellIndex(11, 10)];
        wall.buildingType = _game_building_wall;
        wall.buildingOwnerId = 88;
        wall.buildingHp = GAME_WALL_MAX_HP;
        for (int y = 0; y < GAME_MAP_HEIGHT; ++y) {
            if (y != 10) {
                server.m_cells[server.cellIndex(11, y)].terrain = _game_terrain_mountain;
            }
        }
        DefenseGameServer::PlayerState player;
        player.userId = 88;
        player.online = true;
        player.x = 12.5f;
        player.y = 10.5f;
        server.m_players.insert(player.userId, player);
        server.rebuildFlowField(1000);
        DefenseGameServer::ZombieState blocked;
        blocked.id = 404;
        blocked.x = 10.5f;
        blocked.y = 10.5f;
        blocked.lastProgressAtMs = 100;
        server.moveZombieWithFlowField(blocked, 1000, 0.05f);
        if (wall.buildingHp != GAME_WALL_MAX_HP - GAME_ZOMBIE_BASE_ATTACK) {
            qCritical() << "Blocked zombie did not attack wall" << wall.buildingHp;
            return false;
        }
        return true;
    }

    static bool crowdKeepsFlowingThroughSingleCellGap()
    {
        DefenseGameServer server;
        server.initializeMap();

        DefenseGameServer::PlayerState player;
        player.userId = 405;
        player.online = true;
        player.x = 50.5f;
        player.y = 27.5f;
        server.m_players.insert(player.userId, player);

        for (int x = 0; x < GAME_MAP_WIDTH; ++x) {
            if (x != 50) {
                server.m_cells[server.cellIndex(x, 20)].terrain = _game_terrain_mountain;
            }
        }
        for (int id = 0; id < 18; ++id) {
            DefenseGameServer::ZombieState zombie;
            zombie.id = 500 + id;
            zombie.x = 48.4f + (id % 6) * 0.42f;
            zombie.y = 14.0f + (id / 6) * 0.42f;
            zombie.hp = 100;
            zombie.maxHp = 100;
            server.m_zombies.insert(zombie.id, zombie);
        }
        server.rebuildFlowField(1000);
        for (int step = 1; step <= 700; ++step) {
            server.processZombies(1000 + step * 50, 0.05f);
        }

        int crossed = 0;
        for (auto zombie = server.m_zombies.constBegin();
             zombie != server.m_zombies.constEnd(); ++zombie) {
            if (zombie->y > 21.0f) {
                ++crossed;
            }
        }
        return crossed >= 12;
    }

    static bool deathPreservesZoneAssetsAndRespawnsAtInitialPosition()
    {
        DefenseGameServer server;
        server.initializeMap();

        DefenseGameServer::PlayerState player;
        player.userId = 3;
        player.online = true;
        player.x = 8.5f;
        player.y = 8.5f;
        player.spawnX = 2.5f;
        player.spawnY = 3.5f;
        player.hp = 10;
        server.m_players.insert(player.userId, player);

        const int unprotectedBuildingIndex = server.cellIndex(7, 8);
        server.m_cells[unprotectedBuildingIndex].buildingType = _game_building_turret;
        server.m_cells[unprotectedBuildingIndex].buildingOwnerId = player.userId;
        server.m_cells[unprotectedBuildingIndex].buildingHp = GAME_TURRET_MAX_HP;

        DefenseGameServer::ZoneState zone;
        zone.id = 7;
        zone.ownerId = player.userId;
        zone.state = _game_zone_stabilizing;
        const int zoneCellIndex = server.cellIndex(4, 4);
        zone.cells.insert(zoneCellIndex);
        server.m_cells[zoneCellIndex].zoneId = zone.id;
        server.m_cells[zoneCellIndex].buildingType = _game_building_wall;
        server.m_cells[zoneCellIndex].buildingOwnerId = player.userId;
        server.m_cells[zoneCellIndex].buildingHp = GAME_WALL_MAX_HP;
        server.m_zones.insert(zone.id, zone);

        DefenseGameServer::ZoneState permanentZone;
        permanentZone.id = 8;
        permanentZone.ownerId = player.userId;
        permanentZone.state = _game_zone_permanent;
        const int permanentCellIndex = server.cellIndex(5, 5);
        permanentZone.cells.insert(permanentCellIndex);
        server.m_cells[permanentCellIndex].zoneId = permanentZone.id;
        server.m_cells[permanentCellIndex].buildingType = _game_building_heavy_turret;
        server.m_cells[permanentCellIndex].buildingOwnerId = player.userId;
        server.m_cells[permanentCellIndex].buildingHp = GAME_HEAVY_TURRET_MAX_HP;
        server.m_zones.insert(permanentZone.id, permanentZone);

        server.damagePlayer(server.m_players[player.userId], GAME_ZOMBIE_BASE_ATTACK, 1000);
        const DefenseGameServer::PlayerState& dead = server.m_players[player.userId];
        if (dead.hp != 0 || dead.respawnAtMs != 11000
            || server.m_cells[unprotectedBuildingIndex].buildingType != _game_building_none
            || server.m_cells[zoneCellIndex].buildingType != _game_building_wall
            || server.m_cells[zoneCellIndex].zoneId != zone.id
            || server.m_cells[permanentCellIndex].buildingType
                != _game_building_heavy_turret
            || server.m_cells[permanentCellIndex].zoneId != permanentZone.id
            || !server.m_zones.contains(zone.id)
            || !server.m_zones.contains(permanentZone.id)) {
            return false;
        }

        server.processPlayerRespawns(10999);
        if (server.m_players[player.userId].hp != 0) {
            return false;
        }
        server.processPlayerRespawns(11000);
        const DefenseGameServer::PlayerState& respawned = server.m_players[player.userId];
        if (respawned.hp != respawned.maxHp || respawned.respawnAtMs != 0
            || respawned.invulnerableUntilMs != 16000
            || !qFuzzyCompare(respawned.x, respawned.spawnX)
            || !qFuzzyCompare(respawned.y, respawned.spawnY)) {
            return false;
        }
        server.damagePlayer(server.m_players[player.userId], 25, 15999);
        if (server.m_players[player.userId].hp != server.m_players[player.userId].maxHp) {
            return false;
        }
        server.damagePlayer(server.m_players[player.userId], 25, 16000);
        return server.m_players[player.userId].hp
            == server.m_players[player.userId].maxHp - 25;
    }

    static bool offlinePlayersAreExcludedFromBattleSnapshot()
    {
        DefenseGameServer server;
        server.initializeMap();

        DefenseGameServer::PlayerState onlinePlayer;
        onlinePlayer.userId = 41;
        onlinePlayer.online = true;
        server.m_players.insert(onlinePlayer.userId, onlinePlayer);

        DefenseGameServer::PlayerState offlinePlayer;
        offlinePlayer.userId = 42;
        offlinePlayer.online = true;
        server.m_players.insert(offlinePlayer.userId, offlinePlayer);
        server.m_sessions.insert(902, offlinePlayer.userId);
        server.handleDisconnected(902);

        const STRU_GAME_STATE_RS snapshot = server.makeSnapshot();
        return snapshot.m_playerCount == 1
            && snapshot.m_players[0].m_userId == onlinePlayer.userId
            && snapshot.m_players[0].m_online == 1
            && server.m_players.contains(offlinePlayer.userId)
            && !server.m_players.value(offlinePlayer.userId).online;
    }

    static bool killRewardsAndBuildingUpgradeWork()
    {
        DefenseGameServer server;
        server.initializeMap();

        DefenseGameServer::PlayerState player;
        player.userId = 4;
        player.online = true;
        player.x = 10.5f;
        player.y = 10.5f;
        player.spawnX = player.x;
        player.spawnY = player.y;
        player.level = 2;
        player.coins = GAME_PLAYER_START_COINS;
        player.stone = 11;
        player.aluminum = 12;
        player.iron = 13;
        player.coal = 14;
        player.oil = 15;
        server.m_players.insert(player.userId, player);

        const int turretIndex = server.cellIndex(11, 10);
        DefenseGameServer::CellState& turret = server.m_cells[turretIndex];
        turret.buildingType = _game_building_turret;
        turret.buildingOwnerId = player.userId;
        turret.buildingLevel = 1;
        turret.buildingExp = 0;
        turret.buildingHp = GAME_TURRET_MAX_HP;

        server.processUpgrade(server.m_players[player.userId], 11, 10);
        if (turret.buildingLevel != 2 || turret.buildingExp != 0
            || turret.buildingHp != 100
            || server.turretRange(_game_building_turret, 2) != GAME_TURRET_RANGE
            || server.turretRange(_game_building_turret, 10) != GAME_TURRET_RANGE
            || server.m_players[player.userId].coins
                != GAME_PLAYER_START_COINS - server.buildingUpgradeCost(1)) {
            return false;
        }

        DefenseGameServer::ZombieState playerTarget;
        playerTarget.id = 11;
        playerTarget.hp = 10;
        playerTarget.maxHp = 10;
        const int coinsBeforeKill = server.m_players[player.userId].coins;
        server.damageZombie(playerTarget, GAME_PLAYER_ATTACK,
                            player.userId, -1, true);
        if (playerTarget.hp > 0 || server.m_players[player.userId].level != 2
            || server.m_players[player.userId].experience != GAME_PLAYER_KILL_EXP
            || server.m_players[player.userId].coins
                != coinsBeforeKill + GAME_ZOMBIE_COIN_REWARD) {
            return false;
        }
        server.m_players[player.userId].experience = server.playerExperienceRequired(2) - 1;
        DefenseGameServer::ZombieState levelTarget;
        levelTarget.id = 12;
        levelTarget.hp = 10;
        levelTarget.maxHp = 10;
        server.damageZombie(levelTarget, GAME_PLAYER_ATTACK,
                            player.userId, -1, true);
        if (levelTarget.hp > 0 || server.m_players[player.userId].level != 3
            || server.m_players[player.userId].experience != 0
            || server.m_players[player.userId].stone != 11
            || server.m_players[player.userId].aluminum != 12
            || server.m_players[player.userId].iron != 13
            || server.m_players[player.userId].coal != 14
            || server.m_players[player.userId].oil != 15) {
            return false;
        }
        return true;
    }

    static bool levelFiveTurretDealsSplashDamage()
    {
        DefenseGameServer server;
        server.initializeMap();

        DefenseGameServer::PlayerState player;
        player.userId = 43;
        player.online = true;
        player.x = 10.5f;
        player.y = 10.5f;
        server.m_players.insert(player.userId, player);

        const int turretIndex = server.cellIndex(10, 10);
        DefenseGameServer::CellState& turret = server.m_cells[turretIndex];
        turret.buildingType = _game_building_turret;
        turret.buildingOwnerId = player.userId;
        turret.buildingLevel = GAME_TURRET_SPLASH_UNLOCK_LEVEL;
        turret.buildingHp = server.buildingMaxHp(turret.buildingType,
                                                 turret.buildingLevel);

        DefenseGameServer::ZombieState primary;
        primary.id = 201;
        primary.x = 12.5f;
        primary.y = 10.5f;
        primary.hp = 100;
        primary.maxHp = 100;
        server.m_zombies.insert(primary.id, primary);

        DefenseGameServer::ZombieState nearby = primary;
        nearby.id = 202;
        nearby.x = 13.2f;
        server.m_zombies.insert(nearby.id, nearby);

        DefenseGameServer::ZombieState distant = primary;
        distant.id = 203;
        distant.x = 14.0f;
        server.m_zombies.insert(distant.id, distant);

        server.rebuildZombieSpatialIndex();
        server.processTurrets(10000);

        const int directDamage = server.turretAttack(_game_building_turret,
                                                      GAME_TURRET_SPLASH_UNLOCK_LEVEL);
        const int splashDamage = directDamage * GAME_TURRET_SPLASH_DAMAGE_PERCENT / 100;
        return server.m_zombies.value(primary.id).hp == 100 - directDamage
            && server.m_zombies.value(nearby.id).hp == 100 - splashDamage
            && server.m_zombies.value(distant.id).hp == 100;
    }

    static bool noLivingPlayersResetWaveAndZombieStrength()
    {
        DefenseGameServer server;
        server.initializeMap();
        const qint64 nowMs = 200000;

        DefenseGameServer::PlayerState deadPlayer;
        deadPlayer.userId = 44;
        deadPlayer.online = true;
        deadPlayer.hp = 0;
        deadPlayer.respawnAtMs = nowMs + 10000;
        server.m_players.insert(deadPlayer.userId, deadPlayer);

        DefenseGameServer::ZombieState active;
        active.id = 301;
        active.hp = 900;
        active.maxHp = 900;
        server.m_zombies.insert(active.id, active);
        DefenseGameServer::ZombieState suspended = active;
        suspended.id = 302;
        server.m_suspendedZombies.insert(suspended.id, suspended);
        server.m_dormantZombieDensity.fill(9, GAME_MAP_WIDTH * GAME_MAP_HEIGHT);
        server.m_wave = 12;
        server.m_waveAssaultUntilMs = nowMs + 30000;

        server.resetWaveIfNoLivingPlayers(nowMs);
        if (server.m_wave != 0 || !server.m_zombies.isEmpty()
            || !server.m_suspendedZombies.isEmpty()
            || server.m_waveAssaultUntilMs != 0
            || server.m_nextWaveAtMs != nowMs + 45000
            || !server.m_waveResetForNoSurvivors) {
            return false;
        }
        for (int density : server.m_dormantZombieDensity) {
            if (density < 2 || density > 4) {
                return false;
            }
        }

        server.m_wave = 3;
        server.resetWaveIfNoLivingPlayers(nowMs + 1000);
        if (server.m_wave != 3) {
            return false;
        }

        server.m_players[deadPlayer.userId].hp = 100;
        server.m_players[deadPlayer.userId].respawnAtMs = 0;
        server.resetWaveIfNoLivingPlayers(nowMs + 2000);
        if (server.m_waveResetForNoSurvivors) {
            return false;
        }

        server.m_players[deadPlayer.userId].hp = 0;
        server.m_players[deadPlayer.userId].respawnAtMs = nowMs + 12000;
        server.resetWaveIfNoLivingPlayers(nowMs + 3000);
        return server.m_wave == 0 && server.m_waveResetForNoSurvivors;
    }

    static bool scaledCombatRepairAndBuildingRulesWork()
    {
        DefenseGameServer server;
        server.initializeMap();

        DefenseGameServer::PlayerState player;
        player.userId = 6;
        player.online = true;
        player.x = 10.5f;
        player.y = 10.5f;
        player.level = 9;
        player.coins = GAME_PLAYER_START_COINS;
        server.m_players.insert(player.userId, player);
        if (server.playerAttack(player) != GAME_PLAYER_ATTACK + 8 * GAME_PLAYER_ATTACK_PER_LEVEL
            || !qFuzzyCompare(server.playerAttackRange(player),
                              GAME_PLAYER_ATTACK_RANGE + GAME_PLAYER_RANGE_PER_STEP)
            || server.playerAttackIntervalMs(player)
                != GAME_PLAYER_FIRE_INTERVAL_MS
                    - 8 * GAME_PLAYER_FIRE_INTERVAL_REDUCTION_PER_LEVEL_MS) {
            return false;
        }

        const int wallIndex = server.cellIndex(11, 10);
        DefenseGameServer::CellState& wall = server.m_cells[wallIndex];
        wall.buildingType = _game_building_wall;
        wall.buildingOwnerId = player.userId;
        wall.buildingLevel = 1;
        wall.buildingExp = 0;
        wall.buildingHp = 51;
        const int repairCost = server.buildingRepairCost(wall);
        server.processRepair(server.m_players[player.userId], 11, 10);
        if (repairCost != 7 || wall.buildingHp != 51
            || server.m_players[player.userId].coins != GAME_PLAYER_START_COINS
            || !server.m_repairs.contains(wallIndex)) {
            return false;
        }
        const qint64 repairStartedAt = server.m_repairs.value(wallIndex).lastTickAtMs;
        for (int tick = 1; tick <= 7; ++tick) {
            server.processRepairs(repairStartedAt
                + tick * GAME_BUILDING_REPAIR_INTERVAL_MS);
        }
        if (wall.buildingHp != GAME_WALL_MAX_HP
            || server.m_players[player.userId].coins
                != GAME_PLAYER_START_COINS - repairCost
            || server.m_repairs.contains(wallIndex)) {
            return false;
        }
        server.processUpgrade(server.m_players[player.userId], 11, 10);
        return wall.buildingLevel == 2 && wall.buildingExp == 0
            && wall.buildingHp == 150;
    }

    static bool reinforcedZoneIsImpassableAndGiantExplosionDamagesArea()
    {
        DefenseGameServer server;
        server.initializeMap();
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

        DefenseGameServer::ZoneState zone;
        zone.id = 71;
        zone.ownerId = 71;
        zone.state = _game_zone_permanent;
        zone.shieldLayers = 2;
        zone.reinforceUntilMs = nowMs + 60000;
        for (int y = 10; y <= 12; ++y) {
            for (int x = 10; x <= 12; ++x) {
                const int index = server.cellIndex(x, y);
                zone.cells.insert(index);
                server.m_cells[index].zoneId = zone.id;
            }
        }
        server.m_zones.insert(zone.id, zone);
        DefenseGameServer::CellState& protectedWall =
            server.m_cells[server.cellIndex(9, 11)];
        protectedWall.buildingType = _game_building_wall;
        protectedWall.buildingOwnerId = zone.ownerId;
        protectedWall.buildingHp = GAME_WALL_MAX_HP;
        DefenseGameServer::ZombieState blocked;
        blocked.x = 8.5f;
        blocked.y = 11.5f;
        if (server.zombieTraversalCost(9, 11, true, nowMs)
                < std::numeric_limits<float>::max()) {
            return false;
        }

        DefenseGameServer::PlayerState player;
        player.userId = 72;
        player.online = true;
        player.x = 30.5f;
        player.y = 30.5f;
        player.hp = 100;
        player.coins = 0;
        server.m_players.insert(player.userId, player);
        DefenseGameServer::CellState& blastWall = server.m_cells[server.cellIndex(31, 30)];
        blastWall.buildingType = _game_building_wall;
        blastWall.buildingOwnerId = player.userId;
        blastWall.buildingHp = GAME_WALL_MAX_HP;
        DefenseGameServer::ZombieState giant;
        giant.id = 900;
        giant.kind = GAME_ZOMBIE_KIND_GIANT;
        giant.x = 30.5f;
        giant.y = 30.5f;
        giant.hp = 10;
        giant.maxHp = 10;
        server.damageZombie(giant, 20, player.userId, -1, true);
        return giant.hp <= 0
            && server.m_players[player.userId].hp
                == 100 - GAME_GIANT_ZOMBIE_EXPLOSION_PLAYER_DAMAGE
            && blastWall.buildingHp
                == GAME_WALL_MAX_HP - GAME_GIANT_ZOMBIE_EXPLOSION_BUILDING_DAMAGE;
    }

    static bool heavyTurretUsesLongRangeSlowHeavyAttack()
    {
        DefenseGameServer server;
        server.initializeMap();
        DefenseGameServer::PlayerState player;
        player.userId = 7;
        player.online = true;
        player.level = 2;
        server.m_players.insert(player.userId, player);

        const int index = server.cellIndex(10, 10);
        DefenseGameServer::CellState& turret = server.m_cells[index];
        turret.buildingType = _game_building_heavy_turret;
        turret.buildingOwnerId = player.userId;
        turret.buildingLevel = 1;
        turret.buildingHp = GAME_HEAVY_TURRET_MAX_HP;

        DefenseGameServer::ZombieState zombie;
        zombie.id = 30;
        zombie.x = 17.5f;
        zombie.y = 10.5f;
        zombie.hp = 100;
        zombie.maxHp = 100;
        server.m_zombies.insert(zombie.id, zombie);
        server.rebuildZombieSpatialIndex();
        server.processTurrets(2999);
        if (server.m_zombies[zombie.id].hp != 100) {
            return false;
        }
        server.processTurrets(3000);
        if (server.m_zombies[zombie.id].hp != 100 - GAME_HEAVY_TURRET_ATTACK) {
            return false;
        }
        server.processTurrets(5999);
        return server.m_zombies[zombie.id].hp == 100 - GAME_HEAVY_TURRET_ATTACK
            && server.turretRange(_game_building_heavy_turret, 1) == GAME_HEAVY_TURRET_RANGE;
    }

    static bool territoryIncludesEnclosingWallsAndUsesTenMinutes()
    {
        DefenseGameServer server;
        server.initializeMap();
        DefenseGameServer::PlayerState player;
        player.userId = 8;
        player.color = QColor(24, 120, 200);
        server.m_players.insert(player.userId, player);

        for (int y = 10; y <= 14; ++y) {
            for (int x = 10; x <= 14; ++x) {
                if (x != 10 && x != 14 && y != 10 && y != 14) {
                    continue;
                }
                DefenseGameServer::CellState& wall = server.m_cells[server.cellIndex(x, y)];
                wall.buildingType = _game_building_wall;
                wall.buildingOwnerId = player.userId;
                wall.buildingLevel = 1;
                wall.buildingHp = GAME_WALL_MAX_HP;
            }
        }
        constexpr qint64 claimedAt = 5000;
        server.detectNewZones(player.userId, claimedAt);
        if (server.m_zones.size() != 1) {
            return false;
        }
        const DefenseGameServer::ZoneState zone = server.m_zones.constBegin().value();
        if (zone.cells.size() != 25 || zone.stabilizeAtMs != claimedAt + 10LL * 60 * 1000
            || !zone.cells.contains(server.cellIndex(10, 10))
            || !zone.cells.contains(server.cellIndex(12, 12))
            || !server.isZoneFullyEnclosed(zone)) {
            return false;
        }
        DefenseGameServer::CellState& breached = server.m_cells[server.cellIndex(10, 10)];
        breached.buildingType = _game_building_none;
        breached.buildingOwnerId = 0;
        breached.buildingHp = 0;
        return !server.isZoneFullyEnclosed(zone);
    }

    static bool mapFeaturesAndExtractionChainWork()
    {
        DefenseGameServer server;
        server.initializeMap();
        server.generateMapFeatures();
        int mountains = 0;
        int aluminumNodes = 0;
        int ironNodes = 0;
        int coalNodes = 0;
        int oilNodes = 0;
        int aluminumIndex = -1;
        for (int i = 0; i < server.m_cells.size(); ++i) {
            const DefenseGameServer::CellState& cell = server.m_cells.at(i);
            mountains += cell.terrain == _game_terrain_mountain ? 1 : 0;
            aluminumNodes += cell.resourceType == _game_resource_aluminum ? 1 : 0;
            ironNodes += cell.resourceType == _game_resource_iron ? 1 : 0;
            coalNodes += cell.resourceType == _game_resource_coal ? 1 : 0;
            oilNodes += cell.resourceType == _game_resource_oil ? 1 : 0;
            if (cell.resourceType == _game_resource_aluminum && aluminumIndex < 0) {
                aluminumIndex = i;
            }
        }
        if (mountains < 150 || aluminumNodes < ironNodes || ironNodes < oilNodes
            || coalNodes < oilNodes || aluminumIndex < 0) {
            return false;
        }

        DefenseGameServer::PlayerState player;
        player.userId = 9;
        player.coins = 1000;
        player.stone = 100;
        player.x = aluminumIndex % GAME_MAP_WIDTH + 0.5f;
        player.y = aluminumIndex / GAME_MAP_WIDTH + 0.5f;
        server.m_players.insert(player.userId, player);
        const int oldAmount = server.m_cells[aluminumIndex].resourceAmount;
        server.processBuild(server.m_players[player.userId], aluminumIndex % GAME_MAP_WIDTH,
                            aluminumIndex / GAME_MAP_WIDTH,
                            _game_building_aluminum_extractor);
        if (server.m_cells[aluminumIndex].buildingType != _game_building_aluminum_extractor
            || server.m_players[player.userId].coins != 1000 - GAME_ALUMINUM_EXTRACTOR_COIN_COST
            || server.m_players[player.userId].stone
                != 100 - GAME_ALUMINUM_EXTRACTOR_STONE_COST) {
            return false;
        }
        server.processExtractors(5000);
        if (server.m_players[player.userId].aluminum != 2
            || server.m_cells[aluminumIndex].resourceAmount != oldAmount - 2) {
            return false;
        }

        const int collectorIndex = server.cellIndex(31, 30);
        server.m_cells[collectorIndex] = DefenseGameServer::CellState();
        server.m_players[player.userId].x = 30.5f;
        server.m_players[player.userId].y = 30.5f;
        const int coinsBeforeCollector = server.m_players[player.userId].coins;
        const int stoneBeforeCollector = server.m_players[player.userId].stone;
        server.processBuild(server.m_players[player.userId], 31, 30,
                            _game_building_coin_collector);
        if (server.m_cells[collectorIndex].buildingType
                != _game_building_coin_collector
            || server.m_players[player.userId].coins
                != coinsBeforeCollector - GAME_COIN_COLLECTOR_COIN_COST
            || server.m_players[player.userId].stone
                != stoneBeforeCollector - GAME_COIN_COLLECTOR_STONE_COST) {
            return false;
        }
        server.processExtractors(10000);
        return server.m_players[player.userId].coins
            == coinsBeforeCollector - GAME_COIN_COLLECTOR_COIN_COST
                + GAME_COIN_COLLECTOR_YIELD;
    }

    static bool strategicBreachCostAndWallCornerMovementWork()
    {
        DefenseGameServer server;
        server.initializeMap();
        DefenseGameServer::PlayerState player;
        player.userId = 10;
        player.online = true;
        player.x = 7.5f;
        player.y = 5.5f;
        server.m_players.insert(player.userId, player);

        DefenseGameServer::CellState& plannedWall = server.m_cells[server.cellIndex(5, 5)];
        plannedWall.buildingType = _game_building_wall;
        plannedWall.buildingOwnerId = player.userId;
        plannedWall.buildingHp = GAME_WALL_MAX_HP;
        plannedWall.buildingLevel = 1;
        DefenseGameServer::CellState& sideWall = server.m_cells[server.cellIndex(5, 4)];
        sideWall = plannedWall;

        const float wallCost = server.zombieTraversalCost(5, 5, true);
        plannedWall.buildingType = _game_building_turret;
        const float turretCost = server.zombieTraversalCost(5, 5, true);
        plannedWall.buildingType = _game_building_wall;
        if (!(turretCost < wallCost)) {
            return false;
        }

        DefenseGameServer::ZombieState zombie;
        zombie.id = 50;
        zombie.x = 4.75f;
        zombie.y = 4.72f;
        zombie.targetUserId = player.userId;
        zombie.targetCell = QPoint(7, 5);
        zombie.path = {QPoint(4, 4), QPoint(5, 5), QPoint(6, 5), QPoint(7, 5)};
        zombie.pathIndex = 1;
        zombie.pathUsesBreaches = true;
        zombie.plannedBreaches.insert(server.cellIndex(5, 5));
        zombie.pathUpdatedAtMs = 1000;
        zombie.lastProgressAtMs = 1000;
        zombie.lastProgressX = zombie.x;
        zombie.lastProgressY = zombie.y;
        server.m_zombies.insert(zombie.id, zombie);
        server.moveZombie(server.m_zombies[zombie.id], 1000, 0.05f, false);
        return plannedWall.buildingHp == GAME_WALL_MAX_HP - GAME_ZOMBIE_BASE_ATTACK
            && sideWall.buildingHp == GAME_WALL_MAX_HP
            && !server.m_zombies[zombie.id].path.isEmpty();
    }

    static bool playerLevelDoesNotOverflow()
    {
        DefenseGameServer server;
        server.initializeMap();

        DefenseGameServer::PlayerState player;
        player.userId = 5;
        player.level = GAME_MAX_LEVEL;
        server.m_players.insert(player.userId, player);

        DefenseGameServer::ZombieState target;
        target.id = 12;
        target.hp = 1;
        target.maxHp = 1;
        server.damageZombie(target, GAME_PLAYER_ATTACK, player.userId, -1, true);
        return target.hp <= 0 && server.m_players[player.userId].level == GAME_MAX_LEVEL;
    }

    static bool battleBoundaryTransfersPlayerWithoutMovingBuildings()
    {
        DefenseGameServer source;
        source.initializeMap();
        DefenseGameServer::PlayerState player;
        player.userId = 91;
        player.userName = QStringLiteral("traveler");
        player.online = true;
        player.x = 0.31f;
        player.y = 44.5f;
        player.moveX = -1.0f;
        player.hp = 73;
        player.coins = 240;
        player.stone = 32;
        player.level = 4;
        source.m_players.insert(player.userId, player);
        source.m_sessions.insert(901, player.userId);
        DefenseGameServer::CellState& building = source.m_cells[source.cellIndex(8, 8)];
        building.buildingType = _game_building_turret;
        building.buildingOwnerId = player.userId;
        building.buildingHp = 61;

        bool transitionRequested = false;
        source.setMapTransitionHandler(
            [&](ConnectionId socket, qint64 userId, int directionX, int directionY) {
                transitionRequested = socket == 901 && userId == player.userId
                    && directionX == -1 && directionY == 0;
                return transitionRequested;
            });
        source.processPlayerMovement(0.1f);
        if (!transitionRequested) {
            return false;
        }

        DefenseGameServer::PlayerTransferState transfer;
        if (!source.takePlayerForTransition(901, player.userId, transfer)
            || building.buildingType != _game_building_turret
            || building.buildingHp != 61 || source.m_players.contains(player.userId)) {
            return false;
        }

        DefenseGameServer destination;
        destination.initializeMap();
        destination.acceptPlayerTransition(901, player.userId, player.userName,
                                           transfer, -1, 0);
        const auto arrived = destination.m_players.constFind(player.userId);
        return arrived != destination.m_players.constEnd() && arrived->online
            && arrived->x > GAME_MAP_WIDTH - 2.0f
            && qAbs(arrived->y - player.y) < 1.0f
            && arrived->hp == player.hp && arrived->coins == player.coins
            && arrived->stone == player.stone && arrived->level == player.level;
    }

    static bool transitionAndWaveForceZombieMaterialization()
    {
        DefenseGameServer destination;
        destination.initializeMap();
        destination.initializeDormantHordes();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        destination.m_lastDormantMaterializeAtMs = now;

        DefenseGameServer::PlayerTransferState transfer;
        transfer.userName = QStringLiteral("traveler");
        transfer.sourceX = 0.3f;
        transfer.sourceY = 50.5f;
        destination.acceptPlayerTransition(902, 92, transfer.userName,
                                           transfer, -1, 0);
        const int arrivalZombieCount = destination.m_zombies.size();
        if (arrivalZombieCount <= 0) {
            return false;
        }

        destination.m_lastDormantMaterializeAtMs = now;
        destination.spawnWave(now);
        return destination.m_wave == 1
            && destination.m_zombies.size() > arrivalZombieCount;
    }

    static bool wavesMobilizeGlobalHordesOnFixedSchedule()
    {
        DefenseGameServer server;
        server.initializeMap();
        server.initializeDormantHordes();
        DefenseGameServer::PlayerState player;
        player.userId = 93;
        player.online = true;
        player.x = 50.5f;
        player.y = 50.5f;
        server.m_players.insert(player.userId, player);

        const qint64 nowMs = 500000;
        server.rebuildFlowField(nowMs);
        server.spawnWave(nowMs);
        if (server.m_wave != 1
            || server.m_waveAssaultUntilMs
                != nowMs + GAME_WAVE_ASSAULT_SECONDS * 1000LL
            || server.m_nextWaveAtMs
                != nowMs + GAME_WAVE_INTERVAL_SECONDS * 1000LL
            || server.m_zombies.isEmpty()) {
            qCritical() << "wave" << server.m_wave
                        << "assaultUntil" << server.m_waveAssaultUntilMs
                        << "nextWave" << server.m_nextWaveAtMs
                        << "active" << server.m_zombies.size();
            return false;
        }
        server.spawnWave(server.m_nextWaveAtMs);
        if (server.m_wave != 2
            || server.m_nextWaveAtMs
                != nowMs + GAME_WAVE_INTERVAL_SECONDS * 2000LL) {
            qCritical() << "second wave" << server.m_wave
                        << "nextWave" << server.m_nextWaveAtMs;
            return false;
        }
        return true;
    }

    static bool territoryRegenerationAndRandomDisastersWork()
    {
        DefenseGameServer regenServer;
        regenServer.initializeMap();
        DefenseGameServer::PlayerState player;
        player.userId = 95;
        player.online = true;
        player.x = 20.5f;
        player.y = 20.5f;
        player.hp = 70;
        player.maxHp = 100;
        regenServer.m_players.insert(player.userId, player);

        DefenseGameServer::ZoneState zone;
        zone.id = 950;
        zone.ownerId = player.userId;
        zone.state = _game_zone_stabilizing;
        const int territoryCell = regenServer.cellIndex(20, 20);
        zone.cells.insert(territoryCell);
        regenServer.m_cells[territoryCell].zoneId = zone.id;
        regenServer.m_zones.insert(zone.id, zone);

        regenServer.processPlayerRegeneration(1000);
        regenServer.processPlayerRegeneration(
            1000 + GAME_PLAYER_TERRITORY_REGEN_INTERVAL_MS - 1);
        if (regenServer.m_players[player.userId].hp != 70) {
            return false;
        }
        regenServer.processPlayerRegeneration(
            1000 + GAME_PLAYER_TERRITORY_REGEN_INTERVAL_MS);
        if (regenServer.m_players[player.userId].hp
            != 70 + GAME_PLAYER_TERRITORY_REGEN_HP) {
            return false;
        }
        regenServer.m_players[player.userId].x = 30.5f;
        regenServer.m_players[player.userId].y = 30.5f;
        regenServer.processPlayerRegeneration(
            1000 + GAME_PLAYER_TERRITORY_REGEN_INTERVAL_MS * 2);
        if (regenServer.m_players[player.userId].hp
            != 70 + GAME_PLAYER_TERRITORY_REGEN_HP) {
            return false;
        }

        DefenseGameServer outbreakServer;
        outbreakServer.initializeMap();
        outbreakServer.initializeDormantHordes();
        if (outbreakServer.triggerZombieOutbreak(QPointF(50.5, 50.5), 20) != 20
            || outbreakServer.m_zombies.size() != 20) {
            return false;
        }

        DefenseGameServer lightningServer;
        lightningServer.initializeMap();
        DefenseGameServer::PlayerState lightningPlayer;
        lightningPlayer.userId = 96;
        lightningPlayer.online = true;
        lightningPlayer.x = 50.5f;
        lightningPlayer.y = 50.5f;
        lightningPlayer.hp = 100;
        lightningServer.m_players.insert(lightningPlayer.userId, lightningPlayer);
        DefenseGameServer::CellState& wall =
            lightningServer.m_cells[lightningServer.cellIndex(51, 50)];
        wall.buildingType = _game_building_wall;
        wall.buildingOwnerId = lightningPlayer.userId;
        wall.buildingLevel = 1;
        wall.buildingHp = GAME_WALL_MAX_HP;
        DefenseGameServer::ZombieState zombie;
        zombie.id = 960;
        zombie.x = 52.0f;
        zombie.y = 50.5f;
        zombie.hp = 100;
        zombie.maxHp = 100;
        lightningServer.m_zombies.insert(zombie.id, zombie);
        lightningServer.triggerLightningStrike(QPointF(50.5, 50.5), 5000);
        if (lightningServer.m_players[lightningPlayer.userId].hp
                != 100 - GAME_DISASTER_LIGHTNING_PLAYER_DAMAGE
            || wall.buildingHp
                != GAME_WALL_MAX_HP - GAME_DISASTER_LIGHTNING_BUILDING_DAMAGE
            || lightningServer.m_zombies[zombie.id].hp
                != 100 - GAME_DISASTER_LIGHTNING_ZOMBIE_DAMAGE) {
            return false;
        }
        lightningServer.scheduleNextDisaster(10000);
        return lightningServer.m_nextDisasterAtMs
                >= 10000 + GAME_DISASTER_MIN_INTERVAL_SECONDS * 1000LL
            && lightningServer.m_nextDisasterAtMs
                <= 10000 + GAME_DISASTER_MAX_INTERVAL_SECONDS * 1000LL;
    }

    static bool ownedTerritoryIsPreferredAndRespawnZoneCanBeSelected()
    {
        DefenseGameServer server;
        server.initializeMap();
        DefenseGameServer::PlayerState player;
        player.userId = 94;
        player.online = true;
        player.x = 50.5f;
        player.y = 50.5f;
        player.spawnX = player.x;
        player.spawnY = player.y;
        server.m_players.insert(player.userId, player);

        auto addZone = [&](int zoneId, int x, int y, int state, qint64 ownerId) {
            DefenseGameServer::ZoneState zone;
            zone.id = zoneId;
            zone.ownerId = ownerId;
            zone.state = state;
            for (int dy = 0; dy < 3; ++dy) {
                for (int dx = 0; dx < 3; ++dx) {
                    const int index = server.cellIndex(x + dx, y + dy);
                    zone.cells.insert(index);
                    server.m_cells[index].zoneId = zoneId;
                }
            }
            server.m_zones.insert(zoneId, zone);
        };
        addZone(101, 10, 10, _game_zone_stabilizing, player.userId);
        addZone(102, 30, 30, _game_zone_permanent, player.userId);
        addZone(103, 40, 40, _game_zone_permanent, 999);

        server.refreshPlayerRespawnLocation(server.m_players[player.userId]);
        DefenseGameServer::PlayerState& current = server.m_players[player.userId];
        if (current.respawnZoneId != 102
            || server.m_cells.at(server.cellIndex(qFloor(current.spawnX),
                                                  qFloor(current.spawnY))).zoneId != 102) {
            return false;
        }

        server.processSetRespawnZone(current, 11, 11);
        if (current.respawnZoneId != 101) {
            return false;
        }
        server.processSetRespawnZone(current, 41, 41);
        if (current.respawnZoneId != 101) {
            return false;
        }

        current.hp = 1;
        server.damagePlayer(current, 10, 1000);
        server.processPlayerRespawns(11000);
        const int respawnCell = server.cellIndex(qFloor(current.x), qFloor(current.y));
        const STRU_GAME_STATE_RS snapshot = server.makeSnapshot();
        return server.m_cells.at(respawnCell).zoneId == 101
            && snapshot.m_playerCount == 1
            && snapshot.m_players[0].m_respawnZoneId == 101;
    }

    static bool populationBuildingsAndTerritoryRankingWork()
    {
        DefenseGameServer server;
        server.initializeMap();

        DefenseGameServer::PlayerState player;
        player.userId = 201;
        player.userName = QStringLiteral("Builder");
        player.online = true;
        player.x = 50.5f;
        player.y = 50.5f;
        player.level = 20;
        player.coins = 10000;
        player.stone = 10000;
        server.m_players.insert(player.userId, player);

        server.processBuild(server.m_players[player.userId], 51, 50,
                            _game_building_house);
        server.processBuild(server.m_players[player.userId], 50, 51,
                            _game_building_farm);
        server.processBuild(server.m_players[player.userId], 49, 50,
                            _game_building_well);
        const int houseIndex = server.cellIndex(51, 50);
        if (server.m_cells.at(houseIndex).buildingType != _game_building_house
            || server.m_players[player.userId].population != 12) {
            return false;
        }

        STRU_GAME_STATE_RS snapshot = server.makeSnapshot();
        if (snapshot.m_playerCount != 1
            || snapshot.m_players[0].m_population != 12
            || snapshot.m_players[0].m_populationCapacity != 12
            || snapshot.m_players[0].m_foodCapacity != 16
            || snapshot.m_players[0].m_waterCapacity != 18) {
            return false;
        }

        server.processUpgrade(server.m_players[player.userId], 50, 51);
        server.processUpgrade(server.m_players[player.userId], 49, 50);
        snapshot = server.makeSnapshot();
        if (snapshot.m_players[0].m_foodCapacity != 17
            || snapshot.m_players[0].m_waterCapacity != 19) {
            return false;
        }

        for (int upgrade = 0; upgrade < 12; ++upgrade) {
            server.processUpgrade(server.m_players[player.userId], 51, 50);
        }
        if (server.m_cells.at(houseIndex).buildingLevel != GAME_HOUSE_MAX_LEVEL
            || server.m_players[player.userId].population != 14
            || server.housePopulationCapacity(10) != 4) {
            return false;
        }

        server.processDemolish(server.m_players[player.userId], 49, 50);
        server.processPopulation(30000);
        if (server.m_players[player.userId].population != 13) {
            return false;
        }

        DefenseGameServer::PlayerState rival;
        rival.userId = 202;
        rival.userName = QStringLiteral("Rival");
        rival.color = QColor(190, 70, 70);
        rival.online = true;
        rival.x = 60.5f;
        rival.y = 60.5f;
        server.m_players.insert(rival.userId, rival);

        auto addZone = [&](int id, qint64 ownerId, int state, int cells) {
            DefenseGameServer::ZoneState zone;
            zone.id = id;
            zone.ownerId = ownerId;
            zone.state = state;
            zone.color = ownerId == rival.userId ? rival.color : player.color;
            for (int i = 0; i < cells; ++i) {
                zone.cells.insert(i + id * 100);
            }
            server.m_zones.insert(id, zone);
        };
        addZone(201, player.userId, _game_zone_permanent, 7);
        addZone(202, rival.userId, _game_zone_permanent, 12);
        addZone(203, player.userId, _game_zone_stabilizing, 100);

        snapshot = server.makeSnapshot();
        return snapshot.m_territoryRankCount == 2
            && snapshot.m_territoryRanks[0].m_userId == rival.userId
            && snapshot.m_territoryRanks[0].m_permanentCellCount == 12
            && snapshot.m_territoryRanks[1].m_userId == player.userId
            && snapshot.m_territoryRanks[1].m_permanentCellCount == 7;
    }

    static bool turretWorkforceAndWaveShieldWork()
    {
        DefenseGameServer server;
        server.initializeMap();

        DefenseGameServer::PlayerState player;
        player.userId = 301;
        player.userName = QStringLiteral("ShieldOwner");
        player.online = true;
        player.x = 40.5f;
        player.y = 40.5f;
        player.population = 1;
        player.coins = 10000;
        player.stone = 10000;
        player.aluminum = 10000;
        player.iron = 10000;
        player.coal = 10000;
        player.oil = 10000;
        server.m_players.insert(player.userId, player);

        server.processBuild(server.m_players[player.userId], 41, 40,
                            _game_building_turret);
        server.processBuild(server.m_players[player.userId], 40, 41,
                            _game_building_heavy_turret);
        if (server.m_cells.at(server.cellIndex(41, 40)).buildingType
                != _game_building_turret
            || server.m_cells.at(server.cellIndex(40, 41)).buildingType
                != _game_building_none
            || server.turretPopulationUsed(player.userId) != 1) {
            return false;
        }
        STRU_GAME_STATE_RS snapshot = server.makeSnapshot();
        if (snapshot.m_players[0].m_populationUsed != 1) {
            return false;
        }
        server.processDemolish(server.m_players[player.userId], 41, 40);
        server.processBuild(server.m_players[player.userId], 40, 41,
                            _game_building_heavy_turret);
        if (server.turretPopulationUsed(player.userId) != 1) {
            return false;
        }

        const int shieldIndex = server.cellIndex(39, 40);
        DefenseGameServer::CellState& shield = server.m_cells[shieldIndex];
        shield.buildingType = _game_building_wave_shield;
        shield.buildingOwnerId = player.userId;
        shield.buildingLevel = 1;
        shield.buildingHp = GAME_WAVE_SHIELD_MAX_HP;
        DefenseGameServer::CellState& protectedWall =
            server.m_cells[server.cellIndex(39, 41)];
        protectedWall.buildingType = _game_building_wall;
        protectedWall.buildingOwnerId = player.userId;
        protectedWall.buildingLevel = 1;
        protectedWall.buildingHp = GAME_WALL_MAX_HP;

        DefenseGameServer::ZombieState giant;
        giant.id = 301;
        giant.kind = GAME_ZOMBIE_KIND_GIANT;
        giant.x = 40.5f;
        giant.y = 40.5f;
        server.explodeGiantZombie(giant, 1000);
        if (server.m_players[player.userId].hp != 100
            || protectedWall.buildingHp != GAME_WALL_MAX_HP
            || shield.buildingHp != GAME_WAVE_SHIELD_MAX_HP) {
            return false;
        }

        DefenseGameServer::ZombieState intruder;
        intruder.id = 302;
        intruder.x = 37.5f;
        intruder.y = 40.5f;
        intruder.targetSearchRange = 12.0f;
        if (!server.rebuildZombiePath(intruder, 2000)
            || intruder.targetBuildingIndex != shieldIndex) {
            return false;
        }
        const DefenseGameServer::BuildingCost shieldCost =
            server.buildingCost(_game_building_wave_shield);
        return shieldCost.coins == GAME_WAVE_SHIELD_COIN_COST
            && shieldCost.aluminum == GAME_WAVE_SHIELD_ALUMINUM_COST
            && shieldCost.iron == GAME_WAVE_SHIELD_IRON_COST
            && shieldCost.coal == GAME_WAVE_SHIELD_COAL_COST
            && shieldCost.oil == GAME_WAVE_SHIELD_OIL_COST;
    }
};

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    if (!DefenseGamePathfindingTest::avoidsWallWhenOpenRouteExists()) {
        qCritical() << "A* chose a wall despite an open route";
        return 1;
    }
    if (!DefenseGamePathfindingTest::breachesOnlyWhenNoOpenRouteExists()) {
        qCritical() << "A* did not choose the cheapest required breach";
        return 2;
    }
    if (!DefenseGamePathfindingTest::breachesWhenOpenDetourIsFarMoreExpensive()) {
        qCritical() << "A* did not breach when the open detour was strategically worse";
        return 13;
    }
    if (!DefenseGamePathfindingTest::eachZombieUsesItsOwnTargetRange()) {
        qCritical() << "Zombies did not respect their individual target ranges";
        return 17;
    }
    if (!DefenseGamePathfindingTest::zombieTargetPriorityIsPlayerExtractorThenOffensiveBuilding()) {
        qCritical() << "Zombie target priority did not follow player, extractor, turret order";
        return 18;
    }
    if (!DefenseGamePathfindingTest::smallTargetMovementKeepsCurrentPath()) {
        qCritical() << "A* rebuilt too eagerly for a small target movement";
        return 6;
    }
    if (!DefenseGamePathfindingTest::movingTargetTwoCellsInvalidatesPath()) {
        qCritical() << "A* did not react to meaningful target movement";
        return 14;
    }
    if (!DefenseGamePathfindingTest::mountainsRemainImpassable()) {
        qCritical() << "A* crossed an impassable mountain barrier";
        return 15;
    }
    if (!DefenseGamePathfindingTest::overlappingZombiesKeepForwardProgress()) {
        qCritical() << "Overlapping zombies failed to make bounded forward progress";
        return 16;
    }
    if (!DefenseGamePathfindingTest::flowFieldSchedulerAvoidsIndividualAStarPaths()) {
        qCritical() << "Flow-field scheduler fell back to per-zombie A*";
        return 7;
    }
    if (!DefenseGamePathfindingTest::zoneBufferDormantHordesSpatialIndexAndPoolWork()) {
        qCritical() << "Dormant horde buffer, spatial index, or zombie pool failed";
        return 19;
    }
    if (!DefenseGamePathfindingTest::collisionCorrectionAndBlockedWallAttackWork()) {
        qCritical() << "Zombie overlap correction or blocked-wall attack failed";
        return 20;
    }
    if (!DefenseGamePathfindingTest::crowdKeepsFlowingThroughSingleCellGap()) {
        qCritical() << "Zombie crowd jammed at a single-cell breach";
        return 28;
    }
    if (!DefenseGamePathfindingTest::dormantMaterializationUsesClearanceAndCadence()) {
        qCritical() << "Dormant zombie materialization clearance or cadence failed";
        return 21;
    }
    if (!DefenseGamePathfindingTest::distantZombiesSuspendAndRestoreWithoutLosingIdentity()) {
        qCritical() << "Distant zombie did not suspend and restore with its identity";
        return 22;
    }
    if (!DefenseGamePathfindingTest::zombiesInsidePlayerVisionNeverSuspend()) {
        qCritical() << "Visible zombie was suspended while still inside player vision";
        return 23;
    }
    if (!DefenseGamePathfindingTest::observedZombieUsesLocalAggroOutsideGlobalSimulationRange()) {
        qCritical() << "Observed zombie stopped local combat outside global simulation range";
        return 24;
    }
    if (!DefenseGamePathfindingTest::sectorFlowGuidesDistantHordesTowardTargetSector()) {
        qCritical() << "Sector flow did not guide distant hordes toward the target sector";
        return 25;
    }
    if (!DefenseGamePathfindingTest::rvoCrowdAvoidancePreventsCrossingAndWallEntry()) {
        qCritical() << "RVO crowd avoidance crossed agents or entered a wall";
        return 26;
    }
    if (!DefenseGamePathfindingTest::rvoCrowdAvoidanceHandlesActiveZombieLimit()) {
        qCritical() << "RVO crowd avoidance failed at the active zombie limit";
        return 27;
    }
    if (!DefenseGamePathfindingTest::deathPreservesZoneAssetsAndRespawnsAtInitialPosition()) {
        qCritical() << "Player death asset retention or delayed respawn failed";
        return 3;
    }
    if (!DefenseGamePathfindingTest::offlinePlayersAreExcludedFromBattleSnapshot()) {
        qCritical() << "Offline player remained in the battle snapshot";
        return 28;
    }
    if (!DefenseGamePathfindingTest::killRewardsAndBuildingUpgradeWork()) {
        qCritical() << "Kill rewards or building upgrade failed";
        return 4;
    }
    if (!DefenseGamePathfindingTest::levelFiveTurretDealsSplashDamage()) {
        qCritical() << "Level-five turret splash damage failed";
        return 31;
    }
    if (!DefenseGamePathfindingTest::noLivingPlayersResetWaveAndZombieStrength()) {
        qCritical() << "No-survivor wave and zombie reset failed";
        return 32;
    }
    if (!DefenseGamePathfindingTest::playerLevelDoesNotOverflow()) {
        qCritical() << "Player level exceeded the long-session cap";
        return 5;
    }
    if (!DefenseGamePathfindingTest::scaledCombatRepairAndBuildingRulesWork()) {
        qCritical() << "Scaled combat, repair, or direct wall upgrade failed";
        return 8;
    }
    if (!DefenseGamePathfindingTest::reinforcedZoneIsImpassableAndGiantExplosionDamagesArea()) {
        qCritical() << "Reinforced-zone routing or giant zombie explosion failed";
        return 23;
    }
    if (!DefenseGamePathfindingTest::heavyTurretUsesLongRangeSlowHeavyAttack()) {
        qCritical() << "Heavy turret balance or cooldown failed";
        return 9;
    }
    if (!DefenseGamePathfindingTest::territoryIncludesEnclosingWallsAndUsesTenMinutes()) {
        qCritical() << "Territory boundary inclusion or stabilization deadline failed";
        return 10;
    }
    if (!DefenseGamePathfindingTest::mapFeaturesAndExtractionChainWork()) {
        qCritical() << "Map resource distribution or extraction chain failed";
        return 11;
    }
    if (!DefenseGamePathfindingTest::strategicBreachCostAndWallCornerMovementWork()) {
        qCritical() << "Strategic breach cost or wall-corner movement failed";
        return 12;
    }
    if (!DefenseGamePathfindingTest::battleBoundaryTransfersPlayerWithoutMovingBuildings()) {
        qCritical() << "Battle boundary transfer lost player state or moved map buildings";
        return 29;
    }
    if (!DefenseGamePathfindingTest::transitionAndWaveForceZombieMaterialization()) {
        qCritical() << "Cross-map arrival or wave refresh did not materialize zombies";
        return 30;
    }
    if (!DefenseGamePathfindingTest::wavesMobilizeGlobalHordesOnFixedSchedule()) {
        qCritical() << "Global horde wave scheduling failed";
        return 33;
    }
    if (!DefenseGamePathfindingTest::territoryRegenerationAndRandomDisastersWork()) {
        qCritical() << "Territory regeneration or random disaster handling failed";
        return 35;
    }
    if (!DefenseGamePathfindingTest::ownedTerritoryIsPreferredAndRespawnZoneCanBeSelected()) {
        qCritical() << "Owned-territory spawn preference or respawn selection failed";
        return 34;
    }
    if (!DefenseGamePathfindingTest::populationBuildingsAndTerritoryRankingWork()) {
        qCritical() << "Population buildings, supply settlement, or territory ranking failed";
        return 36;
    }
    if (!DefenseGamePathfindingTest::turretWorkforceAndWaveShieldWork()) {
        qCritical() << "Turret workforce limit or wave shield behavior failed";
        return 37;
    }
    qInfo() << "Defense game server tests passed";
    return 0;
}
