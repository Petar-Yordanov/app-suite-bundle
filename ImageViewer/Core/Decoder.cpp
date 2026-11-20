#include "Decoder.hpp"
#include "Metadata.hpp"
#include <QColorSpace>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageIOHandler>
#include <QSet>
#include <QTransform>
#include <libheif/heif.h>
#include <libraw/libraw.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PSD
#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace imageview {
namespace {
const QStringList rawExt={"3fr","arw","cr2","cr3","crw","dcr","dng","erf","fff","iiq","kdc","mef","mos","mrw","nef","nrw","orf","pef","raf","raw","rw2","rwl","sr2","srf","srw","x3f"};
const QStringList heifExt={"heic","heif","hif","avif"};
constexpr qint64 MaxFile=2ll*1024*MiB;
constexpr qint64 MaxIcc=4*MiB;
class Mapping {
    QFile file; uchar* data_=nullptr; qint64 size_=0;
public:
    explicit Mapping(const QString& path):file(path){if(file.open(QIODevice::ReadOnly)&&file.size()>0&&file.size()<MaxFile){size_=file.size();data_=file.map(0,size_);}}
    ~Mapping(){if(data_)file.unmap(data_);}
    std::span<const std::uint8_t> bytes()const{return data_?std::span<const std::uint8_t>(data_,std::size_t(size_)):std::span<const std::uint8_t>();}
};
QString utf(const std::string& s){return QString::fromUtf8(s.data(),qsizetype(s.size()));}
void mergeMeta(Decoded& out,const Metadata& meta){for(const auto& [k,v]:meta.fields)out.metadata.insert(utf(k),utf(v));}
bool canDecodeFull(QSize s){return s.isValid()&&s.width()>0&&s.height()>0&&qint64(s.width())*s.height()<=MaxFullPixels&&s.width()<=65536&&s.height()<=65536;}
QImage orient(QImage image,int o){
    QTransform t;
    switch(o){case 2:return image.mirrored(true,false);case 3:t.rotate(180);break;case 4:return image.mirrored(false,true);
        case 5:t.rotate(90);return image.mirrored(true,false).transformed(QTransform().rotate(270));
        case 6:t.rotate(90);break;case 7:return image.mirrored(true,false).transformed(QTransform().rotate(90));case 8:t.rotate(270);break;default:return image;}
    return image.transformed(t);
}
void profile(QImage& i,const Metadata& m){
    if(!i.colorSpace().isValid()&&!m.icc.empty()){
        auto c=QColorSpace::fromIccProfile(QByteArray(reinterpret_cast<const char*>(m.icc.data()),qsizetype(m.icc.size())));if(c.isValid())i.setColorSpace(c);
    }
}
QImage forDisplay(QImage image){
    if(image.isNull())return {};
    auto bound=boundedSize(image.size());if(bound!=image.size())image=image.scaled(bound,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    if(image.colorSpace().isValid()&&image.colorSpace()!=QColorSpace(QColorSpace::SRgb)){
        image.convertToColorSpace(QColorSpace(QColorSpace::SRgb));
    }
    if(image.depth()>32)image.convertTo(QImage::Format_RGBA8888);
    return image;
}
Decoded qtDecode(const QString& path,bool allowAnimation,const Metadata& meta){
    Decoded out;auto reader=std::make_unique<QImageReader>(path);reader->setAutoTransform(true);
    // Decide from the actual data rather than trusting the filename extension.
    reader->setDecideFormatFromContent(true);
    if(!reader->canRead()){out.error=reader->errorString();return out;}
    out.format=QString::fromLatin1(reader->format()).toUpper();out.original=reader->size();
    auto transform=reader->transformation();
    if(out.original.isValid()){
        auto scaled=boundedSize(out.original);
        if(scaled!=out.original){
            if(!reader->supportsOption(QImageIOHandler::ScaledSize)&&!canDecodeFull(out.original)){
                out.error="Image is too large for this decoder's bounded full-resolution buffer.";return out;
            }
            reader->setScaledSize(scaled);out.reduced=true;
        }
        if(transform.testFlag(QImageIOHandler::TransformationRotate90))out.original.transpose();
        else if(transform==QImageIOHandler::TransformationNone&&meta.orientation>=5)out.original.transpose();
    }
    const auto keys=reader->textKeys();for(const auto& key:keys){if(out.metadata.size()>=64)break;out.metadata.insert(key.left(96),reader->text(key).left(512));}
    const bool animated=reader->supportsAnimation()&&reader->imageCount()!=1;
    if(animated)out.metadata.insert("Animated source",true);
    out.image=reader->read();if(out.image.isNull()){out.error=reader->errorString();return out;}
    profile(out.image,meta);
    if(transform==QImageIOHandler::TransformationNone&&meta.orientation!=1)out.image=orient(std::move(out.image),meta.orientation);
    if(!out.original.isValid())out.original=out.image.size();
    out.image=forDisplay(std::move(out.image));out.reduced|=out.image.size()!=out.original;
    if(animated&&allowAnimation){auto a=std::make_shared<Animation>();a->path=path;a->scaled=reader->scaledSize();a->repeats=reader->loopCount();a->delay=std::clamp(reader->nextImageDelay(),20,10000);a->orientation=transform==QImageIOHandler::TransformationNone?meta.orientation:1;
        if(!meta.icc.empty())a->colorSpace=QColorSpace::fromIccProfile(QByteArray(reinterpret_cast<const char*>(meta.icc.data()),qsizetype(meta.icc.size())));
        a->reader=std::move(reader);out.animation=std::move(a);}
    return out;
}
Decoded heifDecode(std::span<const std::uint8_t> bytes,const QString& suffix){
    Decoded out;out.format=suffix=="avif"?"AVIF":"HEIF";
    if(bytes.empty()){out.error="Cannot map image file.";return out;}
    using Context=std::unique_ptr<heif_context,decltype(&heif_context_free)>;
    using Handle=std::unique_ptr<heif_image_handle,decltype(&heif_image_handle_release)>;
    Context ctx(heif_context_alloc(),heif_context_free);if(!ctx){out.error="Cannot allocate HEIF decoder.";return out;}
    auto e=heif_context_read_from_memory_without_copy(ctx.get(),bytes.data(),bytes.size(),nullptr);
    if(e.code!=heif_error_Ok){out.error=QString::fromUtf8(e.message);return out;}
    heif_image_handle* h=nullptr;e=heif_context_get_primary_image_handle(ctx.get(),&h);Handle handle(h,heif_image_handle_release);
    if(e.code!=heif_error_Ok||!h){out.error="Cannot access primary HEIF image.";return out;}
    out.original=QSize(heif_image_handle_get_width(h),heif_image_handle_get_height(h));
    if(!canDecodeFull(out.original)){out.error="HEIF image exceeds the bounded full decode limit.";return out;}
    heif_item_id exifId=0;
    if(heif_image_handle_get_list_of_metadata_block_IDs(h,"Exif",&exifId,1)>0){
        auto size=heif_image_handle_get_metadata_size(h,exifId);
        if(size>=4&&size<=std::size_t(MaxIcc)){
            std::vector<std::uint8_t> data(size);auto meta=heif_image_handle_get_metadata(h,exifId,data.data());
            if(meta.code==heif_error_Ok){std::size_t offset=4+(std::size_t(data[0])<<24)+(std::size_t(data[1])<<16)+(std::size_t(data[2])<<8)+data[3];if(offset<size)mergeMeta(out,readExif(std::span<const std::uint8_t>(data).subspan(offset)));}
        }
    }
    heif_image* image=nullptr;auto* options=heif_decoding_options_alloc();if(options)options->convert_hdr_to_8bit=1;
    e=heif_decode_image(h,&image,heif_colorspace_RGB,heif_chroma_interleaved_RGBA,options);
    if(options)heif_decoding_options_free(options);
    if(e.code!=heif_error_Ok||!image){if(image)heif_image_release(image);out.error=QString::fromUtf8(e.message);return out;}
    int stride=0;auto* plane=heif_image_get_plane_readonly(image,heif_channel_interleaved,&stride);
    int width=heif_image_get_width(image,heif_channel_interleaved),height=heif_image_get_height(image,heif_channel_interleaved);
    if(!plane||!canDecodeFull({width,height})||stride<qint64(width)*4){heif_image_release(image);out.error="Invalid decoded HEIF buffer.";return out;}
    out.image=QImage(plane,width,height,stride,QImage::Format_RGBA8888,[](void* p){heif_image_release(static_cast<heif_image*>(p));},image);
    auto iccSize=heif_image_handle_get_raw_color_profile_size(h);
    if(iccSize>0&&iccSize<=std::size_t(MaxIcc)){
        QByteArray icc(qsizetype(iccSize),Qt::Uninitialized);if(heif_image_handle_get_raw_color_profile(h,icc.data()).code==heif_error_Ok){auto cs=QColorSpace::fromIccProfile(icc);if(cs.isValid())out.image.setColorSpace(cs);}
    }else{
        heif_color_profile_nclx* nclx=nullptr;
        if(heif_image_handle_get_nclx_color_profile(h,&nclx).code==heif_error_Ok&&nclx){
            QColorSpace cs;
            // NCLX standardized primaries/transfer values: sRGB/BT.709, P3-D65, BT.2020.
            if(int(nclx->color_primaries)==12&&int(nclx->transfer_characteristics)==13)cs=QColorSpace(QColorSpace::DisplayP3);
            else if(int(nclx->color_primaries)==1)cs=QColorSpace(QColorSpace::SRgb);
            else if(int(nclx->color_primaries)==9&&int(nclx->transfer_characteristics)==13)cs=QColorSpace(QColorSpace::Primaries::Bt2020,QColorSpace::TransferFunction::SRgb);
            if(cs.isValid())out.image.setColorSpace(cs);heif_nclx_color_profile_free(nclx);
        }
    }
    out.original=out.image.size();out.image=forDisplay(std::move(out.image));out.reduced=out.original!=out.image.size();return out;
}
Decoded rawDecode(std::span<const std::uint8_t> bytes,const QString& suffix){
    Decoded out;out.format=suffix.toUpper();if(bytes.empty()){out.error="Cannot map RAW image.";return out;}
    LibRaw raw;auto rc=raw.open_buffer(const_cast<std::uint8_t*>(bytes.data()),bytes.size());
    if(rc!=LIBRAW_SUCCESS){out.error=QString::fromUtf8(libraw_strerror(rc));return out;}
    auto& d=raw.imgdata;out.original={int(d.sizes.width),int(d.sizes.height)};
    if(d.sizes.flip==5||d.sizes.flip==6)out.original.transpose();
    if(!canDecodeFull(out.original)){out.error="RAW image exceeds full decode limit.";return out;}
    out.metadata.insert("Camera make",QString::fromUtf8(d.idata.make));out.metadata.insert("Camera model",QString::fromUtf8(d.idata.model));
    out.metadata.insert("ISO",double(d.other.iso_speed));out.metadata.insert("Exposure (s)",double(d.other.shutter));out.metadata.insert("F-number",double(d.other.aperture));out.metadata.insert("Focal length (mm)",double(d.other.focal_len));
    if(d.other.timestamp>0)out.metadata.insert("Date taken",QDateTime::fromSecsSinceEpoch(d.other.timestamp).toString(Qt::ISODate));
    d.params.output_bps=8;d.params.output_color=1;d.params.use_camera_wb=1;d.params.half_size=boundedSize(out.original)!=out.original;
    if((rc=raw.unpack())!=LIBRAW_SUCCESS||(rc=raw.dcraw_process())!=LIBRAW_SUCCESS){out.error=QString::fromUtf8(libraw_strerror(rc));return out;}
    auto* memory=raw.dcraw_make_mem_image(&rc);
    if(!memory||rc!=LIBRAW_SUCCESS||memory->type!=LIBRAW_IMAGE_BITMAP||memory->bits!=8||memory->colors!=3||!canDecodeFull({memory->width,memory->height})||qint64(memory->width)*memory->height*3>memory->data_size){
        if(memory)LibRaw::dcraw_clear_mem(memory);out.error="Unsupported decoded RAW output.";return out;
    }
    out.image=QImage(memory->data,memory->width,memory->height,qsizetype(memory->width)*3,QImage::Format_RGB888,[](void* p){LibRaw::dcraw_clear_mem(static_cast<libraw_processed_image_t*>(p));},memory);
    out.image.setColorSpace(QColorSpace(QColorSpace::SRgb));out.image=forDisplay(std::move(out.image));out.reduced=out.image.size()!=out.original;return out;
}
Decoded psdDecode(std::span<const std::uint8_t> bytes,const Metadata& meta){
    Decoded out;out.format="PSD";int w=0,h=0,c=0;
    if(bytes.empty()||bytes.size()>std::size_t(std::numeric_limits<int>::max())||!stbi_info_from_memory(bytes.data(),int(bytes.size()),&w,&h,&c)||!canDecodeFull({w,h})){
        out.error="Unsupported PSD or image exceeds decode limit.";return out;
    }
    auto* pixels=stbi_load_from_memory(bytes.data(),int(bytes.size()),&w,&h,&c,4);
    if(!pixels){out.error=QString::fromUtf8(stbi_failure_reason());return out;}
    out.image=QImage(pixels,w,h,qsizetype(w)*4,QImage::Format_RGBA8888,[](void* p){stbi_image_free(p);},pixels);
    profile(out.image,meta);if(meta.orientation!=1)out.image=orient(std::move(out.image),meta.orientation);
    out.original=out.image.size();out.image=forDisplay(std::move(out.image));out.reduced=out.original!=out.image.size();return out;
}
}
QSize boundedSize(QSize s){
    if(s.width()<=0||s.height()<=0)return {};
    const double factor=std::min({1.0,double(MaxDisplayEdge)/s.width(),double(MaxDisplayEdge)/s.height(),std::sqrt(double(MaxDisplayPixels)/(double(s.width())*s.height()))});
    return {std::max(1,int(s.width()*factor)),std::max(1,int(s.height()*factor))};
}
QStringList extensions(){
    QStringList list=rawExt+heifExt+QStringList{"psd"};
    for(const auto& type:QImageReader::supportedImageFormats())list.append(QString::fromLatin1(type).toLower());
    list.removeDuplicates();list.sort(Qt::CaseInsensitive);return list;
}
QStringList neighbors(const QString& path){
    QFileInfo info(path);QDir dir(info.absolutePath());const auto ext=extensions();QStringList result;
    const auto files=dir.entryInfoList(QDir::Files|QDir::Readable|QDir::Hidden,QDir::Name|QDir::IgnoreCase);
    for(const auto& f:files){if(ext.contains(f.suffix(),Qt::CaseInsensitive)||f.absoluteFilePath()==info.absoluteFilePath())result.append(f.absoluteFilePath());
        else if(f.suffix().isEmpty()){QImageReader r(f.absoluteFilePath());r.setDecideFormatFromContent(true);if(r.canRead())result.append(f.absoluteFilePath());}}
    if(!result.contains(info.absoluteFilePath())&&info.exists())result.append(info.absoluteFilePath());
    std::sort(result.begin(),result.end(),[](const QString& a,const QString& b){int c=a.compare(b,Qt::CaseInsensitive);return c==0?a<b:c<0;});
    return result;
}
QString fingerprint(const QString& path){QFileInfo info(path);return path+QChar(0x1f)+QString::number(info.size())+QChar(0x1f)+QString::number(info.lastModified().toMSecsSinceEpoch());}
Decoded decode(const QString& path,bool allowAnimation){
    QFileInfo fi(path);Decoded out;if(!fi.isFile()||!fi.isReadable()){out.error="Cannot read this image file.";return out;}
    if(fi.size()>=MaxFile){out.error="Image file exceeds the 2 GiB input limit.";return out;}
    Mapping map(path);auto bytes=map.bytes();auto metadata=readMetadata(bytes);auto suffix=fi.suffix().toLower();
    bool psd=bytes.size()>=4&&std::memcmp(bytes.data(),"8BPS",4)==0;
    bool heif=heifExt.contains(suffix);
    if(bytes.size()>=12&&std::memcmp(bytes.data()+4,"ftyp",4)==0){auto type=heif_check_filetype(bytes.data(),int(std::min<std::size_t>(bytes.size(),128)));heif|=type==heif_filetype_yes_supported;}
    if(psd)out=psdDecode(bytes,metadata);
    else if(heif)out=heifDecode(bytes,suffix);
    else if(rawExt.contains(suffix))out=rawDecode(bytes,suffix);
    else out=qtDecode(path,allowAnimation,metadata);
    if(out.image.isNull()&&!psd&&(heif||rawExt.contains(suffix))){auto fallback=qtDecode(path,allowAnimation,metadata);if(!fallback.image.isNull())out=std::move(fallback);}
    if(!out.image.isNull()){
        mergeMeta(out,metadata);out.metadata.insert("File size",fi.size());out.metadata.insert("Dimensions",QStringLiteral("%1 × %2").arg(out.original.width()).arg(out.original.height()));out.metadata.insert("Format",out.format);
        out.metadata.insert("Color profile",out.image.colorSpace().isValid()?QStringLiteral("sRGB display (profile applied)"):QStringLiteral("Unspecified; assumed sRGB"));
    }
    return out;
}
Frame nextFrame(Animation& a){
    Frame result;
    if(!a.reader)return {{},0,{},true};
    if(!a.reader->canRead()){
        if(a.repeats==0)return {{},0,{},true};if(a.repeats>0)--a.repeats;
        a.reader=std::make_unique<QImageReader>(a.path);a.reader->setAutoTransform(true);a.reader->setDecideFormatFromContent(true);if(a.scaled.isValid())a.reader->setScaledSize(a.scaled);
    }
    result.image=a.reader->read();if(result.image.isNull()){result.ended=true;result.error=a.reader->errorString();return result;}
    if(a.orientation!=1)result.image=orient(std::move(result.image),a.orientation);
    if(!result.image.colorSpace().isValid()&&a.colorSpace.isValid())result.image.setColorSpace(a.colorSpace);
    result.image=forDisplay(std::move(result.image));result.delay=std::clamp(a.reader->nextImageDelay(),20,10000);return result;
}
}
