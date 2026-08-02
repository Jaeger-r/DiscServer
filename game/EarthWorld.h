#ifndef EARTHWORLD_H
#define EARTHWORLD_H

#include <QHash>
#include <QByteArray>
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QVector>

#include <array>

#include "../Packdef.h"

class CMySql;

class EarthWorld
{
public:
    bool initialize(const QString& dataRoot, CMySql* database);
    bool isReady() const;

    QPoint lonLatToWorldCell(double longitude, double latitude) const;
    QPointF worldCellToLonLat(double worldX, double worldY) const;
    static QPoint battleRegionForPosition(const QPoint& worldPosition);
    QPoint playerPosition(qint64 userId);
    bool movePlayerPosition(qint64 userId, int deltaX, int deltaY,
                            QPoint* updatedPosition = nullptr);
    bool isChunkWithinPlayerRange(int chunkX, int chunkY, const QPoint& playerPosition) const;
    bool makeChunkResponse(qint64 userId, int chunkX, int chunkY,
                           STRU_EARTH_CHUNK_RS& response);
    QByteArray overviewPayload();

    int terrainAtWorldCell(int worldX, int worldY) const;
    int cachedChunkCount() const;

private:
    struct LandPolygon {
        QVector<QPolygonF> rings;
        QRectF bounds;
    };

    struct RiverLine {
        QPolygonF points;
        QRectF bounds;
        QPointF labelPoint;
        QString name;
        int rank = 7;
    };

    struct PlaceLabel {
        QPoint position;
        QString name;
        int rank = 9;
    };

    struct CachedChunk {
        std::array<std::uint8_t, EARTH_CHUNK_CELL_COUNT> cells{};
        QVector<PlaceLabel> labels;
        qint64 lastAccess = 0;
    };

    bool loadLand(const QString& filePath);
    bool loadRivers(const QString& filePath);
    bool loadPlaces(const QString& filePath);
    bool loadRegionLabels(const QString& filePath);
    bool isLand(const QPointF& worldPoint) const;
    CachedChunk generateChunk(int chunkX, int chunkY) const;
    quint64 chunkKey(int chunkX, int chunkY) const;
    void evictOldChunks();
    QByteArray buildOverviewPayload() const;

    CMySql* m_database = nullptr;
    QVector<LandPolygon> m_landPolygons;
    QVector<RiverLine> m_rivers;
    QVector<PlaceLabel> m_places;
    QHash<quint64, CachedChunk> m_chunkCache;
    QByteArray m_overviewPayload;
    qint64 m_accessCounter = 0;
    bool m_ready = false;
};

#endif // EARTHWORLD_H
