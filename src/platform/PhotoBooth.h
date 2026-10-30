#pragma once

#include <QColor>
#include <QObject>
#include <QStringList>

class NotchController;

// The photo booth, as `photoBooth` in QML: the camera's frames (files the camera page saved) become one picture in the chosen template and
// filter (PhotoModel.h), which then goes onto the shelf. Pictures and frames live in a temporary folder that is emptied afterwards.
class PhotoBooth : public QObject
{
    Q_OBJECT
public:
    explicit PhotoBooth(NotchController *notch, QObject *parent = nullptr);
    ~PhotoBooth() override { cleanup(); }

    Q_INVOKABLE int shots(const QString &templateName) const;                  // how many camera frames the template needs
    Q_INVOKABLE QString folder();                                              // where the camera page saves its frames (created)
    Q_INVOKABLE QString compose(const QString &templateName, const QString &filterName, bool mirror, const QStringList &frames, const QColor &accent); // the finished JPEG's path, "" if it failed
    Q_INVOKABLE QString keep(const QString &path);                             // onto the shelf; the new item's id, "" if it failed
    Q_INVOKABLE void cleanup();                                                // removes the temporary folder

private:
    NotchController *m_notch;
    QString m_folder;
};
