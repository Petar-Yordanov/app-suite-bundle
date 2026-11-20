#include "Updater.hpp"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSysInfo>
#include <QVersionNumber>

namespace {
constexpr qint64 ManifestLimit=64*1024,PackageLimit=256ll*1024*1024;
bool https(const QUrl& url){return url.isValid()&&url.scheme()=="https"&&!url.host().isEmpty()&&url.userInfo().isEmpty();}
QNetworkRequest request(const QUrl& url){
    QNetworkRequest r(url);r.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
    r.setTransferTimeout(30000);return r;
}
QString platformKey(){
#if defined(Q_OS_WIN)
    QString os="windows";
#elif defined(Q_OS_MACOS)
    QString os="macos";
#else
    QString os="linux";
#endif
    return os+"-"+QSysInfo::currentCpuArchitecture();
}
}
Updater::Updater(QString directory,QObject* parent):QObject(parent),directory_(std::move(directory)){
    timer_.setInterval(6*60*60*1000);timer_.setTimerType(Qt::VeryCoarseTimer);
    connect(&timer_,&QTimer::timeout,this,&Updater::check);
    setStatus("Automatic updates disabled (no release feed configured).");
}
void Updater::setStatus(QString status){status_=std::move(status);emit changed();}
void Updater::configure(const QString& input){
    if(reply_){reply_->disconnect(this);reply_->abort();reply_->deleteLater();reply_=nullptr;}
    timer_.stop();feed_=QUrl(input.trimmed());package_.clear();
    if(input.trimmed().isEmpty()){setStatus("Automatic updates disabled (no release feed configured).");return;}
    if(!https(feed_)){setStatus("Enter a valid HTTPS release manifest URL.");return;}
    setStatus("Automatic update checks enabled.");timer_.start();QTimer::singleShot(1500,this,&Updater::check);
}
void Updater::check(){
    if(reply_)return;
    if(!https(feed_)){setStatus("Configure a trusted HTTPS release feed first.");return;}
    setStatus("Checking for updates…");
    auto* r=network_.get(request(feed_));reply_=r;r->setReadBufferSize(ManifestLimit);
    auto body=std::make_shared<QByteArray>();
    connect(r,&QNetworkReply::readyRead,this,[r,body]{
        body->append(r->readAll());if(body->size()>ManifestLimit)r->abort();
    });
    connect(r,&QNetworkReply::finished,this,[this,r,body]{
        reply_=nullptr;body->append(r->readAll());r->deleteLater();
        if(r->error()!=QNetworkReply::NoError||!https(r->url())||body->size()>ManifestLimit){setStatus("Update check failed.");return;}
        auto doc=QJsonDocument::fromJson(*body);auto obj=doc.object();
        auto version=QVersionNumber::fromString(obj.value("version").toString());
        if(version.isNull()){setStatus("Invalid release manifest.");return;}
        if(version<=QVersionNumber::fromString(QCoreApplication::applicationVersion())){setStatus("You have the latest release.");return;}
        auto artifact=obj.value("artifacts").toObject().value(platformKey()).toObject();
        QUrl url(artifact.value("url").toString());auto hex=artifact.value("sha256").toString().toLatin1().toLower();
        auto name=artifact.value("filename").toString();
        if(!https(url)||hex.size()!=64||!QRegularExpression("^[0-9a-f]{64}$").match(QString::fromLatin1(hex)).hasMatch()
            ||!QRegularExpression("^[A-Za-z0-9][A-Za-z0-9._-]{0,100}$").match(name).hasMatch()
            || !(name.endsWith(".msi")||name.endsWith(".exe")||name.endsWith(".dmg")||name.endsWith(".pkg")||name.endsWith(".deb")||name.endsWith(".rpm")||name.endsWith(".zip")||name.endsWith(".tar.gz"))){
            setStatus("No valid update for this platform.");return;
        }
        download(url,QByteArray::fromHex(hex),name);
    });
}
void Updater::download(QUrl url,QByteArray digest,QString name){
    if(!QDir().mkpath(directory_)){setStatus("Cannot create download directory.");return;}
    const auto file=QDir(directory_).filePath(name);
    auto out=std::make_shared<QSaveFile>(file);
    if(!out->open(QIODevice::WriteOnly)){setStatus("Cannot save update.");return;}
    auto hash=std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
    auto written=std::make_shared<qint64>(0);auto failed=std::make_shared<bool>(false);
    auto* r=network_.get(request(url));reply_=r;r->setReadBufferSize(64*1024);setStatus("Downloading update…");
    auto drain=[r,out,hash,written,failed]{
        auto data=r->readAll();
        if(*failed)return;
        if(data.size()>PackageLimit-*written||out->write(data)!=data.size()){*failed=true;r->abort();return;}
        *written+=data.size();hash->addData(data);
    };
    connect(r,&QNetworkReply::readyRead,this,drain);
    connect(r,&QNetworkReply::finished,this,[this,r,out,hash,digest,file,failed,drain]{
        reply_=nullptr;drain();r->deleteLater();
        if(*failed||r->error()!=QNetworkReply::NoError||!https(r->url())||hash->result()!=digest||!out->commit()){
            out->cancelWriting();setStatus("Update download failed or checksum did not match.");return;
        }
        // Keep at most one downloaded update; only known files in this dedicated directory.
        for(const auto& info:QDir(directory_).entryInfoList(QDir::Files|QDir::NoDotAndDotDot))
            if(info.absoluteFilePath()!=QFileInfo(file).absoluteFilePath())QFile::remove(info.absoluteFilePath());
        package_=file;setStatus("Verified update downloaded. Open the package to install.");
    });
}
void Updater::install(){
    if(package_.isEmpty()||!QFileInfo::exists(package_)){setStatus("No downloaded update.");return;}
    if(!QDesktopServices::openUrl(QUrl::fromLocalFile(package_)))setStatus("Unable to open the update package.");
}
