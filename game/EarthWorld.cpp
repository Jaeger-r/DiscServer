#include "EarthWorld.h"

#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include <algorithm>
#include <cstring>
#include <list>
#include <string>

#include "../CMySql.h"

namespace {
constexpr double kEarthRadiusKm = 6371.0088;
constexpr double kStandardParallelRadians = M_PI / 6.0;
constexpr double kProjectionScale = 0.8660254037844386;
constexpr int kMaximumCachedChunks = 128;

QJsonDocument readJson(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    return error.error == QJsonParseError::NoError ? document : QJsonDocument();
}

QString localizedName(const QJsonObject& properties)
{
    const QString chinese = properties.value(QStringLiteral("NAME_ZH")).toString().trimmed().isEmpty()
        ? properties.value(QStringLiteral("name_zh")).toString().trimmed()
        : properties.value(QStringLiteral("NAME_ZH")).toString().trimmed();
    if (!chinese.isEmpty()) {
        return chinese;
    }
    const QString name = properties.value(QStringLiteral("NAME")).toString().trimmed().isEmpty()
        ? properties.value(QStringLiteral("name")).toString().trimmed()
        : properties.value(QStringLiteral("NAME")).toString().trimmed();
    return name;
}
}

bool EarthWorld::initialize(const QString& dataRoot, CMySql* database)
{
    m_database = database;
    m_landPolygons.clear();
    m_rivers.clear();
    m_places.clear();
    m_chunkCache.clear();
    m_overviewPayload.clear();

    const bool landLoaded = loadLand(dataRoot + QStringLiteral("/ne_110m_land.geojson"));
    const bool riversLoaded = loadRivers(
        dataRoot + QStringLiteral("/ne_110m_rivers_lake_centerlines.geojson"));
    const bool placesLoaded = loadPlaces(
        dataRoot + QStringLiteral("/ne_110m_populated_places.geojson"));
    const bool regionsLoaded = loadRegionLabels(
        dataRoot + QStringLiteral("/ne_110m_geography_regions_polys.geojson"));
    const bool marineLoaded = loadRegionLabels(
        dataRoot + QStringLiteral("/ne_110m_geography_marine_polys.geojson"));
    const bool pointFeaturesLoaded = loadRegionLabels(
        dataRoot + QStringLiteral("/ne_110m_geography_regions_points.geojson"));
    m_ready = landLoaded && riversLoaded && placesLoaded && regionsLoaded
        && marineLoaded && pointFeaturesLoaded;
    return m_ready;
}

bool EarthWorld::isReady() const
{
    return m_ready;
}

QPoint EarthWorld::lonLatToWorldCell(double longitude, double latitude) const
{
    longitude = std::clamp(longitude, -180.0, 180.0);
    latitude = std::clamp(latitude, -90.0, 90.0);
    const double projectedX = kEarthRadiusKm * qDegreesToRadians(longitude)
        * kProjectionScale;
    const double projectedY = kEarthRadiusKm * qSin(qDegreesToRadians(latitude))
        / kProjectionScale;
    return QPoint(
        qBound(0, qFloor(EARTH_WORLD_WIDTH / 2.0 + projectedX), EARTH_WORLD_WIDTH - 1),
        qBound(0, qFloor(EARTH_WORLD_HEIGHT / 2.0 - projectedY), EARTH_WORLD_HEIGHT - 1));
}

QPointF EarthWorld::worldCellToLonLat(double worldX, double worldY) const
{
    const double projectedX = worldX - EARTH_WORLD_WIDTH / 2.0;
    const double projectedY = EARTH_WORLD_HEIGHT / 2.0 - worldY;
    const double longitude = qRadiansToDegrees(projectedX
        / (kEarthRadiusKm * kProjectionScale));
    const double sinLatitude = std::clamp(projectedY * kProjectionScale
        / kEarthRadiusKm, -1.0, 1.0);
    return QPointF(longitude, qRadiansToDegrees(qAsin(sinLatitude)));
}

QPoint EarthWorld::battleRegionForPosition(const QPoint& worldPosition)
{
    return QPoint(worldPosition.x() / EARTH_BATTLE_REGION_SIZE_CELLS,
                  worldPosition.y() / EARTH_BATTLE_REGION_SIZE_CELLS);
}

QPoint EarthWorld::playerPosition(qint64 userId)
{
    const QPoint fallback = lonLatToWorldCell(121.4737, 31.2304);
    if (!m_database || userId <= 0) {
        return fallback;
    }

    const QString query = QStringLiteral(
        "select world_x, world_y from earth_player_position where user_id = %1")
        .arg(userId);
    std::list<std::string> rows;
    if (m_database->SelectMySql(query.toUtf8().constData(), 2, rows)
        && rows.size() >= 2) {
        const int x = QString::fromUtf8(rows.front().c_str()).toInt();
        rows.pop_front();
        const int y = QString::fromUtf8(rows.front().c_str()).toInt();
        return QPoint(qBound(0, x, EARTH_WORLD_WIDTH - 1),
                      qBound(0, y, EARTH_WORLD_HEIGHT - 1));
    }

    const QString insert = QStringLiteral(
        "insert into earth_player_position(user_id, world_x, world_y) "
        "values(%1, %2, %3) on conflict(user_id) do nothing")
        .arg(userId).arg(fallback.x()).arg(fallback.y());
    m_database->UpdateMySql(insert.toUtf8().constData());
    return fallback;
}

bool EarthWorld::movePlayerPosition(qint64 userId, int deltaX, int deltaY,
                                    QPoint* updatedPosition)
{
    if (!m_database || userId <= 0 || (deltaX == 0 && deltaY == 0)) {
        return false;
    }
    const QPoint current = playerPosition(userId);
    const QPoint next(current.x() + deltaX, current.y() + deltaY);
    if (next.x() < 0 || next.y() < 0 || next.x() >= EARTH_WORLD_WIDTH
        || next.y() >= EARTH_WORLD_HEIGHT) {
        return false;
    }
    const QString update = QStringLiteral(
        "update earth_player_position set world_x = %1, world_y = %2, "
        "updated_at = current_timestamp where user_id = %3")
        .arg(next.x()).arg(next.y()).arg(userId);
    if (!m_database->UpdateMySql(update.toUtf8().constData())) {
        return false;
    }
    if (updatedPosition) {
        *updatedPosition = next;
    }
    return true;
}

bool EarthWorld::isChunkWithinPlayerRange(int chunkX, int chunkY,
                                          const QPoint& playerPosition) const
{
    Q_UNUSED(playerPosition);
    const int chunkColumns = (EARTH_WORLD_WIDTH + EARTH_CHUNK_SIZE - 1)
        / EARTH_CHUNK_SIZE;
    const int chunkRows = (EARTH_WORLD_HEIGHT + EARTH_CHUNK_SIZE - 1)
        / EARTH_CHUNK_SIZE;
    if (chunkX < 0 || chunkY < 0 || chunkX >= chunkColumns || chunkY >= chunkRows) {
        return false;
    }
    return true;
}

bool EarthWorld::makeChunkResponse(qint64 userId, int chunkX, int chunkY,
                                   STRU_EARTH_CHUNK_RS& response)
{
    if (!m_ready) {
        return false;
    }
    const QPoint player = playerPosition(userId);
    if (!isChunkWithinPlayerRange(chunkX, chunkY, player)) {
        return false;
    }

    const quint64 key = chunkKey(chunkX, chunkY);
    auto it = m_chunkCache.find(key);
    if (it == m_chunkCache.end()) {
        it = m_chunkCache.insert(key, generateChunk(chunkX, chunkY));
    }
    it->lastAccess = ++m_accessCounter;

    response = STRU_EARTH_CHUNK_RS();
    response.m_chunkX = chunkX;
    response.m_chunkY = chunkY;
    response.m_playerWorldX = player.x();
    response.m_playerWorldY = player.y();
    std::copy(it->cells.cbegin(), it->cells.cend(), response.m_cells);

    QVector<PlaceLabel> labels = it->labels;
    std::sort(labels.begin(), labels.end(), [](const PlaceLabel& left,
                                               const PlaceLabel& right) {
        if (left.rank != right.rank) {
            return left.rank < right.rank;
        }
        return left.name < right.name;
    });
    response.m_labelCount = static_cast<std::uint16_t>(
        qMin(labels.size(), qsizetype(EARTH_MAX_LABELS)));
    for (int index = 0; index < response.m_labelCount; ++index) {
        const PlaceLabel& source = labels.at(index);
        EarthMapLabel& target = response.m_labels[index];
        target.m_worldX = source.position.x();
        target.m_worldY = source.position.y();
        target.m_rank = static_cast<std::uint8_t>(qBound(0, source.rank, 9));
        const QByteArray nameBytes = source.name.toUtf8();
        qstrncpy(target.m_name, nameBytes.constData(), EARTH_LABEL_NAME_SIZE);
    }
    evictOldChunks();
    return true;
}

QByteArray EarthWorld::overviewPayload()
{
    if (!m_ready) {
        return {};
    }
    if (m_overviewPayload.isEmpty()) {
        m_overviewPayload = buildOverviewPayload();
    }
    return m_overviewPayload;
}

int EarthWorld::terrainAtWorldCell(int worldX, int worldY) const
{
    if (worldX < 0 || worldY < 0 || worldX >= EARTH_WORLD_WIDTH
        || worldY >= EARTH_WORLD_HEIGHT) {
        return _earth_terrain_ocean;
    }
    return isLand(QPointF(worldX + 0.5, worldY + 0.5))
        ? _earth_terrain_land : _earth_terrain_ocean;
}

int EarthWorld::cachedChunkCount() const
{
    return m_chunkCache.size();
}

QByteArray EarthWorld::buildOverviewPayload() const
{
    QImage terrain(EARTH_OVERVIEW_WIDTH, EARTH_OVERVIEW_HEIGHT,
                   QImage::Format_Grayscale8);
    terrain.fill(_earth_terrain_ocean);

    QPainter painter(&terrain);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.scale(1.0 / EARTH_OVERVIEW_SAMPLE_CELLS,
                  1.0 / EARTH_OVERVIEW_SAMPLE_CELLS);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(_earth_terrain_land, _earth_terrain_land,
                            _earth_terrain_land));
    for (const LandPolygon& polygon : m_landPolygons) {
        QPainterPath path;
        path.setFillRule(Qt::OddEvenFill);
        for (const QPolygonF& ring : polygon.rings) {
            path.addPolygon(ring);
        }
        painter.drawPath(path);
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(_earth_terrain_river, _earth_terrain_river,
                              _earth_terrain_river),
                        EARTH_OVERVIEW_SAMPLE_CELLS * 0.8,
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    for (const RiverLine& river : m_rivers) {
        painter.drawPolyline(river.points);
    }
    painter.end();

    EarthOverviewHeader header;
    header.m_labelCount = m_places.size();
    QByteArray payload;
    payload.reserve(sizeof(header) + EARTH_OVERVIEW_WIDTH * EARTH_OVERVIEW_HEIGHT
                    + header.m_labelCount * int(sizeof(EarthMapLabel)));
    payload.append(reinterpret_cast<const char*>(&header), sizeof(header));
    for (int y = 0; y < terrain.height(); ++y) {
        payload.append(reinterpret_cast<const char*>(terrain.constScanLine(y)),
                       terrain.width());
    }
    for (const PlaceLabel& place : m_places) {
        EarthMapLabel label;
        label.m_worldX = place.position.x();
        label.m_worldY = place.position.y();
        label.m_rank = static_cast<std::uint8_t>(qBound(0, place.rank, 9));
        const QByteArray name = place.name.toUtf8();
        qstrncpy(label.m_name, name.constData(), EARTH_LABEL_NAME_SIZE);
        payload.append(reinterpret_cast<const char*>(&label), sizeof(label));
    }
    return payload;
}

bool EarthWorld::loadLand(const QString& filePath)
{
    const QJsonDocument document = readJson(filePath);
    if (!document.isObject()) {
        return false;
    }
    const QJsonArray features = document.object().value(QStringLiteral("features")).toArray();
    for (const QJsonValue& featureValue : features) {
        const QJsonObject geometry = featureValue.toObject()
            .value(QStringLiteral("geometry")).toObject();
        const QString type = geometry.value(QStringLiteral("type")).toString();
        const QJsonArray coordinates = geometry.value(QStringLiteral("coordinates")).toArray();
        QJsonArray polygons;
        if (type == QStringLiteral("Polygon")) {
            polygons.append(coordinates);
        } else if (type == QStringLiteral("MultiPolygon")) {
            polygons = coordinates;
        } else {
            continue;
        }

        for (const QJsonValue& polygonValue : polygons) {
            LandPolygon polygon;
            const QJsonArray rings = polygonValue.toArray();
            for (const QJsonValue& ringValue : rings) {
                QPolygonF ring;
                for (const QJsonValue& coordinateValue : ringValue.toArray()) {
                    const QJsonArray coordinate = coordinateValue.toArray();
                    if (coordinate.size() < 2) {
                        continue;
                    }
                    ring.append(lonLatToWorldCell(coordinate.at(0).toDouble(),
                                                  coordinate.at(1).toDouble()));
                }
                if (ring.size() >= 3) {
                    polygon.rings.append(ring);
                }
            }
            if (!polygon.rings.isEmpty()) {
                polygon.bounds = polygon.rings.first().boundingRect();
                m_landPolygons.append(std::move(polygon));
            }
        }
    }
    return !m_landPolygons.isEmpty();
}

bool EarthWorld::loadRivers(const QString& filePath)
{
    const QJsonDocument document = readJson(filePath);
    if (!document.isObject()) {
        return false;
    }
    const QJsonArray features = document.object().value(QStringLiteral("features")).toArray();
    for (const QJsonValue& featureValue : features) {
        const QJsonObject feature = featureValue.toObject();
        const QJsonObject geometry = feature.value(QStringLiteral("geometry")).toObject();
        const QJsonObject properties = feature.value(QStringLiteral("properties")).toObject();
        const QString type = geometry.value(QStringLiteral("type")).toString();
        const QJsonArray coordinates = geometry.value(QStringLiteral("coordinates")).toArray();
        QJsonArray lines;
        if (type == QStringLiteral("LineString")) {
            lines.append(coordinates);
        } else if (type == QStringLiteral("MultiLineString")) {
            lines = coordinates;
        } else {
            continue;
        }
        for (const QJsonValue& lineValue : lines) {
            RiverLine line;
            for (const QJsonValue& coordinateValue : lineValue.toArray()) {
                const QJsonArray coordinate = coordinateValue.toArray();
                if (coordinate.size() >= 2) {
                    line.points.append(lonLatToWorldCell(coordinate.at(0).toDouble(),
                                                        coordinate.at(1).toDouble()));
                }
            }
            if (line.points.size() < 2) {
                continue;
            }
            line.bounds = line.points.boundingRect();
            line.labelPoint = line.points.at(line.points.size() / 2);
            line.name = localizedName(properties);
            line.rank = qBound(3, properties.value(QStringLiteral("scalerank")).toInt(4) + 3, 9);
            m_rivers.append(std::move(line));
        }
    }
    return !m_rivers.isEmpty();
}

bool EarthWorld::loadPlaces(const QString& filePath)
{
    const QJsonDocument document = readJson(filePath);
    if (!document.isObject()) {
        return false;
    }
    const QJsonArray features = document.object().value(QStringLiteral("features")).toArray();
    for (const QJsonValue& featureValue : features) {
        const QJsonObject feature = featureValue.toObject();
        const QJsonObject geometry = feature.value(QStringLiteral("geometry")).toObject();
        const QJsonObject properties = feature.value(QStringLiteral("properties")).toObject();
        const QJsonArray coordinate = geometry.value(QStringLiteral("coordinates")).toArray();
        const QString name = localizedName(properties);
        if (geometry.value(QStringLiteral("type")).toString() != QStringLiteral("Point")
            || coordinate.size() < 2 || name.isEmpty()) {
            continue;
        }
        PlaceLabel label;
        label.position = lonLatToWorldCell(coordinate.at(0).toDouble(),
                                           coordinate.at(1).toDouble());
        label.name = name;
        label.rank = qBound(0, properties.value(QStringLiteral("SCALERANK")).toInt(9), 9);
        m_places.append(std::move(label));
    }
    return !m_places.isEmpty();
}

bool EarthWorld::loadRegionLabels(const QString& filePath)
{
    const QJsonDocument document = readJson(filePath);
    if (!document.isObject()) {
        return false;
    }
    const QJsonArray features = document.object().value(QStringLiteral("features")).toArray();
    int loadedCount = 0;
    for (const QJsonValue& featureValue : features) {
        const QJsonObject feature = featureValue.toObject();
        const QJsonObject geometry = feature.value(QStringLiteral("geometry")).toObject();
        const QJsonObject properties = feature.value(QStringLiteral("properties")).toObject();
        const QString name = localizedName(properties);
        if (name.isEmpty()) {
            continue;
        }

        const QString type = geometry.value(QStringLiteral("type")).toString();
        const QJsonArray coordinates = geometry.value(QStringLiteral("coordinates")).toArray();
        QPoint position(-1, -1);
        if (type == QStringLiteral("Point") && coordinates.size() >= 2) {
            position = lonLatToWorldCell(coordinates.at(0).toDouble(),
                                         coordinates.at(1).toDouble());
        } else {
            QJsonArray polygons;
            if (type == QStringLiteral("Polygon")) {
                polygons.append(coordinates);
            } else if (type == QStringLiteral("MultiPolygon")) {
                polygons = coordinates;
            }
            QRectF largestBounds;
            double largestArea = -1.0;
            for (const QJsonValue& polygonValue : polygons) {
                const QJsonArray rings = polygonValue.toArray();
                if (rings.isEmpty()) {
                    continue;
                }
                QPolygonF outerRing;
                for (const QJsonValue& coordinateValue : rings.first().toArray()) {
                    const QJsonArray coordinate = coordinateValue.toArray();
                    if (coordinate.size() >= 2) {
                        outerRing.append(lonLatToWorldCell(coordinate.at(0).toDouble(),
                                                          coordinate.at(1).toDouble()));
                    }
                }
                const QRectF bounds = outerRing.boundingRect();
                const double area = bounds.width() * bounds.height();
                if (outerRing.size() >= 3 && area > largestArea) {
                    largestArea = area;
                    largestBounds = bounds;
                }
            }
            if (largestArea >= 0.0) {
                position = largestBounds.center().toPoint();
            }
        }
        if (position.x() < 0 || position.y() < 0) {
            continue;
        }

        const int rawRank = properties.contains(QStringLiteral("SCALERANK"))
            ? properties.value(QStringLiteral("SCALERANK")).toInt(3)
            : properties.value(QStringLiteral("scalerank")).toInt(3);
        m_places.append({position, name, qBound(0, rawRank + 1, 8)});
        ++loadedCount;
    }
    return loadedCount > 0;
}

bool EarthWorld::isLand(const QPointF& worldPoint) const
{
    for (const LandPolygon& polygon : m_landPolygons) {
        if (!polygon.bounds.contains(worldPoint)
            || !polygon.rings.first().containsPoint(worldPoint, Qt::OddEvenFill)) {
            continue;
        }
        bool inHole = false;
        for (int ringIndex = 1; ringIndex < polygon.rings.size(); ++ringIndex) {
            if (polygon.rings.at(ringIndex).containsPoint(worldPoint, Qt::OddEvenFill)) {
                inHole = true;
                break;
            }
        }
        if (!inHole) {
            return true;
        }
    }
    return false;
}

EarthWorld::CachedChunk EarthWorld::generateChunk(int chunkX, int chunkY) const
{
    CachedChunk chunk;
    const int originX = chunkX * EARTH_CHUNK_SIZE;
    const int originY = chunkY * EARTH_CHUNK_SIZE;
    const QRectF worldBounds(originX, originY, EARTH_CHUNK_SIZE, EARTH_CHUNK_SIZE);
    for (int localY = 0; localY < EARTH_CHUNK_SIZE; ++localY) {
        for (int localX = 0; localX < EARTH_CHUNK_SIZE; ++localX) {
            const int worldX = originX + localX;
            const int worldY = originY + localY;
            const int index = localY * EARTH_CHUNK_SIZE + localX;
            if (worldX < EARTH_WORLD_WIDTH && worldY < EARTH_WORLD_HEIGHT
                && isLand(QPointF(worldX + 0.5, worldY + 0.5))) {
                chunk.cells[index] = _earth_terrain_land;
            }
        }
    }

    QImage riverMask(EARTH_CHUNK_SIZE, EARTH_CHUNK_SIZE, QImage::Format_Grayscale8);
    riverMask.fill(0);
    QPainter riverPainter(&riverMask);
    riverPainter.setRenderHint(QPainter::Antialiasing, false);
    riverPainter.setPen(QPen(Qt::white, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    for (const RiverLine& river : m_rivers) {
        if (!river.bounds.intersects(worldBounds)) {
            continue;
        }
        QPolygonF localLine;
        localLine.reserve(river.points.size());
        for (const QPointF& point : river.points) {
            localLine.append(point - QPointF(originX, originY));
        }
        riverPainter.drawPolyline(localLine);
        if (!river.name.isEmpty() && worldBounds.contains(river.labelPoint)) {
            chunk.labels.append({river.labelPoint.toPoint(), river.name, river.rank});
        }
    }
    riverPainter.end();
    for (int localY = 0; localY < EARTH_CHUNK_SIZE; ++localY) {
        const uchar* maskLine = riverMask.constScanLine(localY);
        for (int localX = 0; localX < EARTH_CHUNK_SIZE; ++localX) {
            const int index = localY * EARTH_CHUNK_SIZE + localX;
            if (maskLine[localX] > 0 && chunk.cells[index] == _earth_terrain_land) {
                chunk.cells[index] = _earth_terrain_river;
            }
        }
    }

    for (const PlaceLabel& place : m_places) {
        if (worldBounds.contains(place.position)) {
            chunk.labels.append(place);
        }
    }
    return chunk;
}

quint64 EarthWorld::chunkKey(int chunkX, int chunkY) const
{
    return (quint64(static_cast<quint32>(chunkX)) << 32)
        | static_cast<quint32>(chunkY);
}

void EarthWorld::evictOldChunks()
{
    while (m_chunkCache.size() > kMaximumCachedChunks) {
        auto oldest = m_chunkCache.begin();
        for (auto it = m_chunkCache.begin(); it != m_chunkCache.end(); ++it) {
            if (it->lastAccess < oldest->lastAccess) {
                oldest = it;
            }
        }
        m_chunkCache.erase(oldest);
    }
}
