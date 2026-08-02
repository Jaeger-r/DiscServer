#include <QCoreApplication>
#include <QDebug>
#include <QtMath>

#include <cstring>

#include "game/EarthWorld.h"

namespace {
bool near(double actual, double expected, double tolerance)
{
    return qAbs(actual - expected) <= tolerance;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    EarthWorld world;
    if (!world.initialize(QStringLiteral(EARTH_TEST_DATA_PATH), nullptr)) {
        qCritical() << "Unable to load Earth test data";
        return 1;
    }

    const struct Sample {
        double longitude;
        double latitude;
    } samples[] = {
        {121.4737, 31.2304},
        {-0.1276, 51.5072},
        {-74.0060, 40.7128},
        {179.0, -16.0}
    };
    for (const Sample& sample : samples) {
        const QPoint cell = world.lonLatToWorldCell(sample.longitude, sample.latitude);
        const QPointF roundTrip = world.worldCellToLonLat(cell.x() + 0.5, cell.y() + 0.5);
        if (!near(roundTrip.x(), sample.longitude, 0.03)
            || !near(roundTrip.y(), sample.latitude, 0.03)) {
            qCritical() << "Projection round trip failed" << sample.longitude
                        << sample.latitude << cell << roundTrip;
            return 2;
        }
    }

    const QPoint shanghai = world.lonLatToWorldCell(121.4737, 31.2304);
    const QPoint pacific = world.lonLatToWorldCell(-150.0, 0.0);
    if (world.terrainAtWorldCell(shanghai.x(), shanghai.y()) == _earth_terrain_ocean
        || world.terrainAtWorldCell(pacific.x(), pacific.y()) != _earth_terrain_ocean) {
        qCritical() << "Natural Earth land classification failed" << shanghai << pacific;
        return 3;
    }

    const QPoint battleOrigin(12340, 5670);
    const QPoint battleRegion = EarthWorld::battleRegionForPosition(battleOrigin);
    if (EARTH_BATTLE_REGION_SIZE_CELLS != 10
        || EarthWorld::battleRegionForPosition(battleOrigin + QPoint(9, 9))
            != battleRegion
        || EarthWorld::battleRegionForPosition(
               battleOrigin + QPoint(EARTH_BATTLE_REGION_SIZE_CELLS, 0))
            != battleRegion + QPoint(1, 0)) {
        qCritical() << "Earth-to-battle 10x10 region mapping failed";
        return 10;
    }

    STRU_EARTH_CHUNK_RS response;
    if (!world.makeChunkResponse(1, shanghai.x() / EARTH_CHUNK_SIZE,
                                 shanghai.y() / EARTH_CHUNK_SIZE, response)) {
        qCritical() << "Shanghai chunk generation failed";
        return 4;
    }
    bool hasShanghai = false;
    for (int i = 0; i < response.m_labelCount; ++i) {
        const QString name = QString::fromUtf8(response.m_labels[i].m_name);
        hasShanghai = hasShanghai || name == QStringLiteral("上海")
            || name.compare(QStringLiteral("Shanghai"), Qt::CaseInsensitive) == 0;
    }
    if (!hasShanghai) {
        qCritical() << "Shanghai label missing from generated chunk";
        return 5;
    }

    const int distantChunkX = pacific.x() / EARTH_CHUNK_SIZE;
    const int distantChunkY = pacific.y() / EARTH_CHUNK_SIZE;
    if (!world.isChunkWithinPlayerRange(distantChunkX, distantChunkY, shanghai)
        || world.isChunkWithinPlayerRange(-1, distantChunkY, shanghai)) {
        qCritical() << "Global chunk access or world bounds validation failed";
        return 6;
    }

    const QByteArray overview = world.overviewPayload();
    if (overview.size() < static_cast<int>(sizeof(EarthOverviewHeader))) {
        qCritical() << "Earth overview generation failed";
        return 7;
    }
    EarthOverviewHeader overviewHeader;
    memcpy(&overviewHeader, overview.constData(), sizeof(overviewHeader));
    const qint64 terrainBytes = qint64(overviewHeader.m_overviewWidth)
        * overviewHeader.m_overviewHeight;
    const qint64 expectedOverviewSize = sizeof(overviewHeader) + terrainBytes
        + qint64(overviewHeader.m_labelCount) * sizeof(EarthMapLabel);
    if (overviewHeader.m_overviewWidth != EARTH_OVERVIEW_WIDTH
        || overviewHeader.m_overviewHeight != EARTH_OVERVIEW_HEIGHT
        || overviewHeader.m_labelCount <= 0
        || overview.size() != expectedOverviewSize) {
        qCritical() << "Earth overview metadata is invalid";
        return 8;
    }
    bool overviewHasLand = false;
    bool overviewHasOcean = false;
    const auto* overviewTerrain = reinterpret_cast<const std::uint8_t*>(
        overview.constData() + sizeof(overviewHeader));
    for (qint64 index = 0; index < terrainBytes; ++index) {
        overviewHasLand = overviewHasLand
            || overviewTerrain[index] == _earth_terrain_land;
        overviewHasOcean = overviewHasOcean
            || overviewTerrain[index] == _earth_terrain_ocean;
    }
    if (!overviewHasLand || !overviewHasOcean) {
        qCritical() << "Earth overview terrain is incomplete";
        return 9;
    }
    return 0;
}
