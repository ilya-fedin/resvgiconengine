/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/
#include "resvgiconengine.h"

#ifndef QT_NO_SVGRENDERER

#include <QPainter>
#include <QPixmap>
#include <QPixmapCache>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QMimeType>
#include <QAtomicInt>
#include <QDebug>
#include <private/qguiapplication_p.h>
#include <ResvgQt.h>

QT_BEGIN_NAMESPACE

class ResvgIconEnginePrivate : public QSharedData
{
public:
    ResvgIconEnginePrivate()
        : svgBuffers(0), addedPixmaps(0)
        { stepSerialNum(); rendererOptions.loadSystemFonts(); }

    ~ResvgIconEnginePrivate()
        { delete addedPixmaps; delete svgBuffers; }

    static int hashKey(QIcon::Mode mode, QIcon::State state)
        { return (((mode)<<4)|state); }

    QString pmcKey(const QSize &size, QIcon::Mode mode, QIcon::State state)
        { return QLatin1String("$qt_svgicon_")
                 + QString::number(serialNum, 16).append(QLatin1Char('_'))
                 + QString::number((((((qint64(size.width()) << 11) | size.height()) << 11) | mode) << 4) | state, 16); }

    void stepSerialNum()
        { serialNum = lastSerialNum.fetchAndAddRelaxed(1); }

    bool tryLoad(ResvgRenderer *renderer, QIcon::Mode mode, QIcon::State state);
    QIcon::Mode loadDataForModeAndState(ResvgRenderer *renderer, QIcon::Mode mode, QIcon::State state);

    QHash<int, QString> svgFiles;
    QHash<int, QByteArray> *svgBuffers;
    QHash<int, QPixmap> *addedPixmaps;
    int serialNum;
    static QAtomicInt lastSerialNum;
    ResvgOptions rendererOptions;
};

QAtomicInt ResvgIconEnginePrivate::lastSerialNum;

ResvgIconEngine::ResvgIconEngine()
    : d(new ResvgIconEnginePrivate)
{
}

ResvgIconEngine::ResvgIconEngine(const ResvgIconEngine &other)
    : QIconEngine(other), d(new ResvgIconEnginePrivate)
{
    d->svgFiles = other.d->svgFiles;
    if (other.d->svgBuffers)
        d->svgBuffers = new QHash<int, QByteArray>(*other.d->svgBuffers);
    if (other.d->addedPixmaps)
        d->addedPixmaps = new QHash<int, QPixmap>(*other.d->addedPixmaps);
}


ResvgIconEngine::~ResvgIconEngine()
{
}


QSize ResvgIconEngine::actualSize(const QSize &size, QIcon::Mode mode,
                                 QIcon::State state)
{
    if (d->addedPixmaps) {
        QPixmap pm = d->addedPixmaps->value(d->hashKey(mode, state));
        if (!pm.isNull() && pm.size() == size)
            return size;
    }

    QPixmap pm = pixmap(size, mode, state);
    if (pm.isNull())
        return QSize();
    return pm.size();
}

static QByteArray maybeUncompress(const QByteArray &ba)
{
#ifndef QT_NO_COMPRESS
    return qUncompress(ba);
#else
    return ba;
#endif
}

bool ResvgIconEnginePrivate::tryLoad(ResvgRenderer *renderer, QIcon::Mode mode, QIcon::State state)
{
    if (svgBuffers) {
        QByteArray buf = svgBuffers->value(hashKey(mode, state));
        if (!buf.isEmpty()) {
            buf = maybeUncompress(buf);
            renderer->load(buf, rendererOptions);
            return true;
        }
    }
    QString svgFile = svgFiles.value(hashKey(mode, state));
    if (!svgFile.isEmpty()) {
        rendererOptions.setResourcesDir(QFileInfo(svgFile).absolutePath());
        renderer->load(svgFile, rendererOptions);
        return true;
    }
    return false;
}

QIcon::Mode ResvgIconEnginePrivate::loadDataForModeAndState(ResvgRenderer *renderer, QIcon::Mode mode, QIcon::State state)
{
    if (tryLoad(renderer, mode, state))
        return mode;

    const QIcon::State oppositeState = (state == QIcon::On) ? QIcon::Off : QIcon::On;
    if (mode == QIcon::Disabled || mode == QIcon::Selected) {
        const QIcon::Mode oppositeMode = (mode == QIcon::Disabled) ? QIcon::Selected : QIcon::Disabled;
        if (tryLoad(renderer, QIcon::Normal, state))
            return QIcon::Normal;
        if (tryLoad(renderer, QIcon::Active, state))
            return QIcon::Active;
        if (tryLoad(renderer, mode, oppositeState))
            return mode;
        if (tryLoad(renderer, QIcon::Normal, oppositeState))
            return QIcon::Normal;
        if (tryLoad(renderer, QIcon::Active, oppositeState))
            return QIcon::Active;
        if (tryLoad(renderer, oppositeMode, state))
            return oppositeMode;
        if (tryLoad(renderer, oppositeMode, oppositeState))
            return oppositeMode;
    } else {
        const QIcon::Mode oppositeMode = (mode == QIcon::Normal) ? QIcon::Active : QIcon::Normal;
        if (tryLoad(renderer, oppositeMode, state))
            return oppositeMode;
        if (tryLoad(renderer, mode, oppositeState))
            return mode;
        if (tryLoad(renderer, oppositeMode, oppositeState))
            return oppositeMode;
        if (tryLoad(renderer, QIcon::Disabled, state))
            return QIcon::Disabled;
        if (tryLoad(renderer, QIcon::Selected, state))
            return QIcon::Selected;
        if (tryLoad(renderer, QIcon::Disabled, oppositeState))
            return QIcon::Disabled;
        if (tryLoad(renderer, QIcon::Selected, oppositeState))
            return QIcon::Selected;
    }
    return QIcon::Normal;
}

QPixmap ResvgIconEngine::pixmap(const QSize &size, QIcon::Mode mode,
                               QIcon::State state)
{
    QPixmap pm;

    QString pmckey(d->pmcKey(size, mode, state));
    if (QPixmapCache::find(pmckey, &pm))
        return pm;

    if (d->addedPixmaps) {
        pm = d->addedPixmaps->value(d->hashKey(mode, state));
        if (!pm.isNull() && pm.size() == size)
            return pm;
    }

    ResvgRenderer renderer;
    const QIcon::Mode loadmode = d->loadDataForModeAndState(&renderer, mode, state);
    if (!renderer.isValid())
        return pm;

    QSize actualSize = renderer.defaultSize();
    if (!actualSize.isNull())
        actualSize.scale(size, Qt::KeepAspectRatio);

    if (actualSize.isEmpty())
        return QPixmap();

    QImage img = renderer.renderToImage(actualSize);
    pm = QPixmap::fromImage(img);
    if (qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        if (loadmode != mode && mode != QIcon::Normal) {
            const QPixmap generated = QGuiApplicationPrivate::instance()->applyQIconStyleHelper(mode, pm);
            if (!generated.isNull())
                pm = generated;
        }
    }

    if (!pm.isNull())
        QPixmapCache::insert(pmckey, pm);

    return pm;
}


void ResvgIconEngine::addPixmap(const QPixmap &pixmap, QIcon::Mode mode,
                               QIcon::State state)
{
    if (!d->addedPixmaps)
        d->addedPixmaps = new QHash<int, QPixmap>;
    d->stepSerialNum();
    d->addedPixmaps->insert(d->hashKey(mode, state), pixmap);
}

enum FileType { OtherFile, SvgFile, CompressedSvgFile };

static FileType fileType(const QFileInfo &fi)
{
    const QString &abs = fi.absoluteFilePath();
    if (abs.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive))
        return SvgFile;
    if (abs.endsWith(QLatin1String(".svgz"), Qt::CaseInsensitive)
        || abs.endsWith(QLatin1String(".svg.gz"), Qt::CaseInsensitive)) {
        return CompressedSvgFile;
    }
#ifndef QT_NO_MIMETYPE
    const QString &mimeTypeName = QMimeDatabase().mimeTypeForFile(fi).name();
    if (mimeTypeName == QLatin1String("image/svg+xml"))
        return SvgFile;
    if (mimeTypeName == QLatin1String("image/svg+xml-compressed"))
        return CompressedSvgFile;
#endif // !QT_NO_MIMETYPE
    return OtherFile;
}

void ResvgIconEngine::addFile(const QString &fileName, const QSize &,
                             QIcon::Mode mode, QIcon::State state)
{
    if (!fileName.isEmpty()) {
         const QFileInfo fi(fileName);
         const QString abs = fi.absoluteFilePath();
         const QString absDir = fi.absolutePath();
         const FileType type = fileType(fi);
#ifndef QT_NO_COMPRESS
         if (type == SvgFile || type == CompressedSvgFile) {
#else
         if (type == SvgFile) {
#endif
             ResvgRenderer renderer;
             d->rendererOptions.setResourcesDir(absDir);
             renderer.load(abs, d->rendererOptions);
             if (renderer.isValid()) {
                 d->stepSerialNum();
                 d->svgFiles.insert(d->hashKey(mode, state), abs);
             }
         } else if (type == OtherFile) {
             QPixmap pm(abs);
             if (!pm.isNull())
                 addPixmap(pm, mode, state);
         }
    }
}

void ResvgIconEngine::paint(QPainter *painter, const QRect &rect,
                           QIcon::Mode mode, QIcon::State state)
{
    QSize pixmapSize = rect.size();
    if (painter->device())
        pixmapSize *= painter->device()->devicePixelRatioF();
    painter->drawPixmap(rect, pixmap(pixmapSize, mode, state));
}

QString ResvgIconEngine::key() const
{
    return QLatin1String("svg");
}

QIconEngine *ResvgIconEngine::clone() const
{
    return new ResvgIconEngine(*this);
}


bool ResvgIconEngine::read(QDataStream &in)
{
    d = new ResvgIconEnginePrivate;
    d->svgBuffers = new QHash<int, QByteArray>;

    if (in.version() >= QDataStream::Qt_4_4) {
        int isCompressed;
        QHash<int, QString> fileNames;  // For memoryoptimization later
        in >> fileNames >> isCompressed >> *d->svgBuffers;
#ifndef QT_NO_COMPRESS
        if (!isCompressed) {
            for (auto it = d->svgBuffers->begin(), end = d->svgBuffers->end(); it != end; ++it)
                it.value() = qCompress(it.value());
        }
#else
        if (isCompressed) {
            qWarning("ResvgIconEngine: Can not decompress SVG data");
            d->svgBuffers->clear();
        }
#endif
        int hasAddedPixmaps;
        in >> hasAddedPixmaps;
        if (hasAddedPixmaps) {
            d->addedPixmaps = new QHash<int, QPixmap>;
            in >> *d->addedPixmaps;
        }
    }
    else {
        QPixmap pixmap;
        QByteArray data;
        uint mode;
        uint state;
        int num_entries;

        in >> data;
        if (!data.isEmpty()) {
#ifndef QT_NO_COMPRESS
            data = qUncompress(data);
#endif
            if (!data.isEmpty())
                d->svgBuffers->insert(d->hashKey(QIcon::Normal, QIcon::Off), data);
        }
        in >> num_entries;
        for (int i=0; i<num_entries; ++i) {
            if (in.atEnd())
                return false;
            in >> pixmap;
            in >> mode;
            in >> state;
            // The pm list written by 4.3 is buggy and/or useless, so ignore.
            //addPixmap(pixmap, QIcon::Mode(mode), QIcon::State(state));
        }
    }

    return true;
}


bool ResvgIconEngine::write(QDataStream &out) const
{
    if (out.version() >= QDataStream::Qt_4_4) {
        int isCompressed = 0;
#ifndef QT_NO_COMPRESS
        isCompressed = 1;
#endif
        QHash<int, QByteArray> svgBuffers;
        if (d->svgBuffers)
            svgBuffers = *d->svgBuffers;
        for (auto it = d->svgFiles.cbegin(), end = d->svgFiles.cend(); it != end; ++it) {
            QByteArray buf;
            QFile f(it.value());
            if (f.open(QIODevice::ReadOnly))
                buf = f.readAll();
#ifndef QT_NO_COMPRESS
            buf = qCompress(buf);
#endif
            svgBuffers.insert(it.key(), buf);
        }
        out << d->svgFiles << isCompressed << svgBuffers;
        if (d->addedPixmaps)
            out << (int)1 << *d->addedPixmaps;
        else
            out << (int)0;
    }
    else {
        QByteArray buf;
        if (d->svgBuffers)
            buf = d->svgBuffers->value(d->hashKey(QIcon::Normal, QIcon::Off));
        if (buf.isEmpty()) {
            QString svgFile = d->svgFiles.value(d->hashKey(QIcon::Normal, QIcon::Off));
            if (!svgFile.isEmpty()) {
                QFile f(svgFile);
                if (f.open(QIODevice::ReadOnly))
                    buf = f.readAll();
            }
        }
#ifndef QT_NO_COMPRESS
        buf = qCompress(buf);
#endif
        out << buf;
        // 4.3 has buggy handling of added pixmaps, so don't write any
        out << (int)0;
    }
    return true;
}

void ResvgIconEngine::virtual_hook(int id, void *data)
{
    if (id == QIconEngine::IsNullHook) {
        *reinterpret_cast<bool*>(data) = d->svgFiles.isEmpty() && !d->addedPixmaps && (!d->svgBuffers || d->svgBuffers->isEmpty());
    }
    QIconEngine::virtual_hook(id, data);
}

QT_END_NAMESPACE

#endif // QT_NO_SVGRENDERER
