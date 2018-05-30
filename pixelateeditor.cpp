#include <qmath.h>
#include <QFileInfo>
#include <QThread>
#include <QImageReader>
#include <QPainter>

#include "pixelateeditor.h"

PixelateEditor::PixelateEditor(QDeclarativeItem *parent) : QDeclarativeItem(parent)
{
    IsChanged   = false;
    CurrentMode = ModeScroll;
    HelperSize  = 0;
    PixelDenom  = 0;

    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton | Qt::MiddleButton);

    setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);
    setFlag(QGraphicsItem::ItemHasNoContents,           false);
}

PixelateEditor::~PixelateEditor()
{
}

int PixelateEditor::mode() const
{
    return CurrentMode;
}

void PixelateEditor::setMode(const int &mode)
{
    CurrentMode = mode;
}

int PixelateEditor::helperSize() const
{
    return HelperSize;
}

void PixelateEditor::setHelperSize(const int &size)
{
    HelperSize = size;
}

int PixelateEditor::pixDenom() const
{
    return PixelDenom;
}

void PixelateEditor::setPixDenom(const int &pix_denom)
{
    PixelDenom = pix_denom;
}

bool PixelateEditor::changed() const
{
    return IsChanged;
}

void PixelateEditor::openImage(const QString &image_url)
{
    QString image_file = QUrl(image_url).toLocalFile();

    if (!image_file.isNull()) {
        QImageReader reader(image_file);

        if (reader.canRead()) {
            QSize size = reader.size();

            if (size.width() * size.height() > IMAGE_MPIX_LIMIT * 1000000.0) {
                qreal factor = qSqrt((size.width() * size.height()) / (IMAGE_MPIX_LIMIT * 1000000.0));

                size.setWidth(size.width()   / factor);
                size.setHeight(size.height() / factor);

                reader.setScaledSize(size);
            }

            LoadedImage = reader.read();

            if (!LoadedImage.isNull()) {
                LoadedImage = LoadedImage.convertToFormat(QImage::Format_RGB16);

                if (!LoadedImage.isNull()) {
                    QThread                *thread    = new QThread();
                    PixelateImageGenerator *generator = new PixelateImageGenerator();

                    generator->moveToThread(thread);

                    QObject::connect(thread,    SIGNAL(started()),                  generator, SLOT(start()));
                    QObject::connect(thread,    SIGNAL(finished()),                 thread,    SLOT(deleteLater()));
                    QObject::connect(generator, SIGNAL(imageReady(const QImage &)), this,      SLOT(effectedImageReady(const QImage &)));
                    QObject::connect(generator, SIGNAL(finished()),                 thread,    SLOT(quit()));
                    QObject::connect(generator, SIGNAL(finished()),                 generator, SLOT(deleteLater()));

                    generator->setPixelDenom(PixelDenom);
                    generator->setInput(LoadedImage);

                    thread->start(QThread::LowPriority);
                } else {
                    emit imageOpenFailed();
                }
            } else {
                emit imageOpenFailed();
            }
        } else {
            emit imageOpenFailed();
        }
    } else {
        emit imageOpenFailed();
    }
}

void PixelateEditor::saveImage(const QString &image_url)
{
    QString file_name = QUrl(image_url).toLocalFile();

    if (!file_name.isNull()) {
        if (!CurrentImage.isNull()) {
            if (QFileInfo(file_name).suffix().compare("png", Qt::CaseInsensitive) != 0 &&
                QFileInfo(file_name).suffix().compare("jpg", Qt::CaseInsensitive) != 0 &&
                QFileInfo(file_name).suffix().compare("bmp", Qt::CaseInsensitive) != 0) {
                file_name = file_name + ".jpg";
            }

            if (CurrentImage.convertToFormat(QImage::Format_ARGB32).save(file_name)) {
                IsChanged = false;

                emit imageSaved();
            } else {
                emit imageSaveFailed();
            }
        } else {
            emit imageSaveFailed();
        }
    } else {
        emit imageSaveFailed();
    }
}

void PixelateEditor::undo()
{
    if (UndoStack.size() > 0) {
        CurrentImage = UndoStack.pop();

        if (UndoStack.size() == 0) {
            emit undoAvailabilityChanged(false);
        }

        IsChanged = true;

        update();
    }
}

void PixelateEditor::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget*)
{
    qreal scale = 1.0;

    if (CurrentImage.width() != 0 && CurrentImage.height() != 0) {
        scale = width() / CurrentImage.width() < height() / CurrentImage.height() ?
                width() / CurrentImage.width() : height() / CurrentImage.height();
    }

    bool antialiasing = painter->testRenderHint(QPainter::Antialiasing);

    if (smooth()) {
        painter->setRenderHint(QPainter::Antialiasing, true);
    }

    QRectF src_rect(option->exposedRect.left()   / scale,
                    option->exposedRect.top()    / scale,
                    option->exposedRect.width()  / scale,
                    option->exposedRect.height() / scale);

    painter->drawImage(option->exposedRect, CurrentImage.copy(src_rect.toRect()));

    painter->setRenderHint(QPainter::Antialiasing, antialiasing);
}

void PixelateEditor::effectedImageReady(const QImage &effected_image)
{
    OriginalImage = LoadedImage;
    EffectedImage = effected_image;
    CurrentImage  = EffectedImage;

    LoadedImage = QImage();

    UndoStack.clear();

    IsChanged = true;

    setImplicitWidth(CurrentImage.width());
    setImplicitHeight(CurrentImage.height());

    update();

    emit undoAvailabilityChanged(false);
    emit imageOpened();
}

void PixelateEditor::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (CurrentMode == ModeOriginal || CurrentMode == ModeEffected) {
        ChangeImageAt(true, event->pos().x(), event->pos().y());

        emit mouseEvent(MousePressed, event->pos().x(), event->pos().y());
    }
}

void PixelateEditor::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (CurrentMode == ModeOriginal || CurrentMode == ModeEffected) {
        ChangeImageAt(false, event->pos().x(), event->pos().y());

        emit mouseEvent(MouseMoved, event->pos().x(), event->pos().y());
    }
}

void PixelateEditor::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (CurrentMode == ModeOriginal || CurrentMode == ModeEffected) {
        emit mouseEvent(MouseReleased, event->pos().x(), event->pos().y());
    }
}

void PixelateEditor::SaveUndoImage()
{
    UndoStack.push(CurrentImage);

    if (UndoStack.size() > UNDO_DEPTH) {
        for (int i = 0; i < UndoStack.size() - UNDO_DEPTH; i++) {
            UndoStack.remove(0);
        }
    }

    emit undoAvailabilityChanged(true);
}

void PixelateEditor::ChangeImageAt(bool save_undo, int center_x, int center_y)
{
    if (CurrentMode != ModeScroll) {
        if (save_undo) {
            SaveUndoImage();
        }

        qreal scale = 1.0;

        if (CurrentImage.width() != 0 && CurrentImage.height() != 0) {
            scale = width() / CurrentImage.width() < height() / CurrentImage.height() ?
                    width() / CurrentImage.width() : height() / CurrentImage.height();
        }

        int img_center_x = center_x   / scale;
        int img_center_y = center_y   / scale;
        int radius       = BRUSH_SIZE / scale;

        for (int x = img_center_x - radius; x <= img_center_x + radius; x++) {
            for (int y = img_center_y - radius; y <= img_center_y + radius; y++) {
                if (x >= 0 && x < CurrentImage.width() && y >= 0 && y < CurrentImage.height() && qSqrt(qPow(x - img_center_x, 2) + qPow(y - img_center_y, 2)) <= radius) {
                    if (CurrentMode == ModeOriginal) {
                        CurrentImage.setPixel(x, y, OriginalImage.pixel(x, y));
                    } else {
                        CurrentImage.setPixel(x, y, EffectedImage.pixel(x, y));
                    }
                }
            }
        }

        IsChanged = true;

        update(center_x - BRUSH_SIZE, center_y - BRUSH_SIZE, BRUSH_SIZE * 2, BRUSH_SIZE * 2);

        QImage helper_image = CurrentImage.copy(img_center_x - (HelperSize / scale) / 2,
                                                img_center_y - (HelperSize / scale) / 2,
                                                HelperSize / scale,
                                                HelperSize / scale).scaledToWidth(HelperSize);

        emit helperImageReady(helper_image);
    }
}

PixelatePreviewGenerator::PixelatePreviewGenerator(QDeclarativeItem *parent) : QDeclarativeItem(parent)
{
    PixelateGeneratorRunning = false;
    RestartPixelateGenerator = false;
    PixelDenom               = 0;

    setFlag(QGraphicsItem::ItemHasNoContents, false);
}

PixelatePreviewGenerator::~PixelatePreviewGenerator()
{
}

int PixelatePreviewGenerator::pixDenom() const
{
    return PixelDenom;
}

void PixelatePreviewGenerator::setPixDenom(const int &pix_denom)
{
    PixelDenom = pix_denom;

    if (!LoadedImage.isNull()) {
        if (PixelateGeneratorRunning) {
            RestartPixelateGenerator = true;
        } else {
            StartPixelateGenerator();
        }
    }
}

void PixelatePreviewGenerator::openImage(const QString &image_url)
{
    QString image_file = QUrl(image_url).toLocalFile();

    if (!image_file.isNull()) {
        QImageReader reader(image_file);

        if (reader.canRead()) {
            QSize size = reader.size();

            if (size.width() * size.height() > IMAGE_MPIX_LIMIT * 1000000.0) {
                qreal factor = qSqrt((size.width() * size.height()) / (IMAGE_MPIX_LIMIT * 1000000.0));

                size.setWidth(size.width()   / factor);
                size.setHeight(size.height() / factor);

                reader.setScaledSize(size);
            }

            LoadedImage = reader.read();

            if (!LoadedImage.isNull()) {
                LoadedImage = LoadedImage.convertToFormat(QImage::Format_RGB16);

                if (!LoadedImage.isNull()) {
                    emit imageOpened();

                    if (PixelateGeneratorRunning) {
                        RestartPixelateGenerator = true;
                    } else {
                        StartPixelateGenerator();
                    }
                } else {
                    emit imageOpenFailed();
                }
            } else {
                emit imageOpenFailed();
            }
        } else {
            emit imageOpenFailed();
        }
    } else {
        emit imageOpenFailed();
    }
}

void PixelatePreviewGenerator::paint(QPainter *painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    qreal scale = 1.0;

    if (PixelatedImage.width() != 0 && PixelatedImage.height() != 0) {
        scale = width() / PixelatedImage.width() < height() / PixelatedImage.height() ?
                width() / PixelatedImage.width() : height() / PixelatedImage.height();
    }

    bool antialiasing = painter->testRenderHint(QPainter::Antialiasing);

    if (smooth()) {
        painter->setRenderHint(QPainter::Antialiasing, true);
    }

    QRectF src_rect(0, 0,
                    PixelatedImage.width(),
                    PixelatedImage.height());
    QRectF dst_rect((width()  - PixelatedImage.width()  * scale) / 2,
                    (height() - PixelatedImage.height() * scale) / 2,
                    PixelatedImage.width()  * scale,
                    PixelatedImage.height() * scale);

    painter->drawImage(dst_rect, PixelatedImage, src_rect);

    painter->setRenderHint(QPainter::Antialiasing, antialiasing);
}

void PixelatePreviewGenerator::pixelatedImageReady(const QImage &pixelated_image)
{
    PixelateGeneratorRunning = false;
    PixelatedImage           = pixelated_image;

    setImplicitWidth(PixelatedImage.width());
    setImplicitHeight(PixelatedImage.height());

    update();

    emit generationFinished();

    if (RestartPixelateGenerator) {
        StartPixelateGenerator();

        RestartPixelateGenerator = false;
    }
}

void PixelatePreviewGenerator::StartPixelateGenerator()
{
    QThread                *thread    = new QThread();
    PixelateImageGenerator *generator = new PixelateImageGenerator();

    generator->moveToThread(thread);

    QObject::connect(thread,    SIGNAL(started()),                  generator, SLOT(start()));
    QObject::connect(thread,    SIGNAL(finished()),                 thread,    SLOT(deleteLater()));
    QObject::connect(generator, SIGNAL(imageReady(const QImage &)), this,      SLOT(pixelatedImageReady(const QImage &)));
    QObject::connect(generator, SIGNAL(finished()),                 thread,    SLOT(quit()));
    QObject::connect(generator, SIGNAL(finished()),                 generator, SLOT(deleteLater()));

    generator->setPixelDenom(PixelDenom);
    generator->setInput(LoadedImage);

    thread->start(QThread::LowPriority);

    PixelateGeneratorRunning = true;

    emit generationStarted();
}

PixelateImageGenerator::PixelateImageGenerator(QObject *parent) : QObject(parent)
{
    PixelDenom = 0;
}

PixelateImageGenerator::~PixelateImageGenerator()
{
}

void PixelateImageGenerator::setPixelDenom(const int &pix_denom)
{
    PixelDenom = pix_denom;
}

void PixelateImageGenerator::setInput(const QImage &input_image)
{
    InputImage = input_image;
}

void PixelateImageGenerator::start()
{
    QImage pixelated_image = InputImage;

    int pix_size = pixelated_image.width() > pixelated_image.height() ? pixelated_image.width() / PixelDenom : pixelated_image.height() / PixelDenom;

    if (pix_size != 0) {
        for (int i = 0; i < pixelated_image.width() / pix_size + 1; i++) {
            for (int j = 0; j < pixelated_image.height() / pix_size + 1; j++) {
                int avg_r  = 0;
                int avg_g  = 0;
                int avg_b  = 0;
                int pixels = 0;

                for (int x = i * pix_size; x < (i + 1) * pix_size && x < pixelated_image.width(); x++) {
                    for (int y = j * pix_size; y < (j + 1) * pix_size && y < pixelated_image.height(); y++) {
                        int pixel = pixelated_image.pixel(x, y);

                        avg_r += qRed(pixel);
                        avg_g += qGreen(pixel);
                        avg_b += qBlue(pixel);

                        pixels++;
                    }
                }

                if (pixels != 0) {
                    avg_r = avg_r / pixels;
                    avg_g = avg_g / pixels;
                    avg_b = avg_b / pixels;

                    for (int x = i * pix_size; x < (i + 1) * pix_size && x < pixelated_image.width(); x++) {
                        for (int y = j * pix_size; y < (j + 1) * pix_size && y < pixelated_image.height(); y++) {
                            pixelated_image.setPixel(x, y, qRgba(avg_r, avg_g, avg_b, qAlpha(pixelated_image.pixel(x, y))));
                        }
                    }
                }
            }
        }
    }

    emit imageReady(pixelated_image);
    emit finished();
}
