#include "core/dancedisplay.h"

#include "core/application.h"
#include "playlist/playlistmanager.h"

#include <qjson/serializer.h>

#include <QFile>
#include <QRegExp>
#include <QTcpSocket>

DanceDisplay::DanceDisplay(Application* app, QObject* parent)
    : QObject(parent),
      app_(app),
      server_(new QTcpServer(this)) {
  connect(server_, SIGNAL(newConnection()), SLOT(NewConnection()));
  server_->listen(QHostAddress::Any, 5678);
}

void DanceDisplay::NewConnection() {
  QTcpSocket* socket = server_->nextPendingConnection();
  connect(socket, SIGNAL(readyRead()), SLOT(ReadyRead()));
  connect(socket, SIGNAL(disconnected()), SLOT(Disconnected()));
}

void DanceDisplay::ReadyRead() {
  QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());

  QByteArray& buffer = buffer_[socket];
  buffer.append(socket->readAll());

  if (buffer.endsWith("\r\n\r\n")) {
    QRegExp re("GET ([^ ]+) HTTP/.*");
    if (re.indexIn(QString::fromUtf8(buffer)) != 0) {
      socket->write("HTTP/1.0 400 Bad Request\r\n\r\n");
      socket->close();
      return;
    }

    QString path = re.cap(1);
    if (path == "/") {
      socket->write("HTTP/1.0 200 OK\r\n"
                    "Content-Type: text/html;\r\n\r\n");
      socket->write(RenderPage());
    } else if (path == "/data.json") {
      socket->write("HTTP/1.0 200 OK\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Content-Type: text/json;\r\n\r\n");
      socket->write(RenderJson());
    } else {
      socket->write("HTTP/1.0 404 Not Found\r\n\r\n");
    }

    socket->close();
  }
}

QByteArray DanceDisplay::RenderPage() const {
  QFile f(":dancedisplay.html");
  f.open(QIODevice::ReadOnly);
  return f.readAll();
}

QByteArray DanceDisplay::RenderJson() const {
  QVariantMap ret;

  Playlist* playlist = app_->playlist_manager()->active();

  int row = playlist->current_row();
  if (playlist->has_item_at(row)) {
     ret["now"] = SongToData(playlist->item_at(row)->Metadata());
  }

  row = playlist->next_row();
  if (playlist->has_item_at(row)) {
     ret["next"] = SongToData(playlist->item_at(row)->Metadata());
  }

  QJson::Serializer s;
  s.setIndentMode(QJson::IndentCompact);
  return s.serialize(ret);
}

QVariantMap DanceDisplay::SongToData(const Song& song) const {
  QString basename = song.basefilename();
  int idx = basename.lastIndexOf('.');
  if (idx != -1) {
    basename = basename.left(idx);
  }

  QVariantMap ret;
  ret["file_name"] = basename;
  ret["artist"] = song.artist();
  ret["intermediate"] = song.title();
  ret["beginner"] = song.comment();
  ret["partner"] = song.composer();
  ret["lead_follow"] = song.grouping();
  return ret;
}

void DanceDisplay::Disconnected() {
  QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());

  buffer_.remove(socket);
  socket->deleteLater();
}
