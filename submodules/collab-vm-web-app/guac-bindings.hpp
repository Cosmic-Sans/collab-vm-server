#include "Guacamole.capnp.h"

struct ArcWrapper {
	ArcWrapper(Guacamole::Arc::Reader* arc=nullptr) : arc_(arc) {}
	std::int32_t getLayer() const { return arc_->getLayer(); }
	void setLayer(std::int32_t&& layer) { layer_ = std::move(layer); }
	std::int32_t getX() const { return arc_->getX(); }
	void setX(std::int32_t&& x) { x_ = std::move(x); }
	std::int32_t getY() const { return arc_->getY(); }
	void setY(std::int32_t&& y) { y_ = std::move(y); }
	std::int32_t getRadius() const { return arc_->getRadius(); }
	void setRadius(std::int32_t&& radius) { radius_ = std::move(radius); }
	double getStart() const { return arc_->getStart(); }
	void setStart(double&& start) { start_ = std::move(start); }
	double getEnd() const { return arc_->getEnd(); }
	void setEnd(double&& end) { end_ = std::move(end); }
	bool getNegative() const { return arc_->getNegative(); }
	void setNegative(bool&& negative) { negative_ = std::move(negative); }
private:
	Guacamole::Arc::Reader* arc_;
	std::int32_t layer_;
	std::int32_t x_;
	std::int32_t y_;
	std::int32_t radius_;
	double start_;
	double end_;
	bool negative_;
};
struct CfillWrapper {
	CfillWrapper(Guacamole::Cfill::Reader* cfill=nullptr) : cfill_(cfill) {}
	std::uint8_t getMask() const { return cfill_->getMask(); }
	void setMask(std::uint8_t&& mask) { mask_ = std::move(mask); }
	std::int32_t getLayer() const { return cfill_->getLayer(); }
	void setLayer(std::int32_t&& layer) { layer_ = std::move(layer); }
	std::int32_t getR() const { return cfill_->getR(); }
	void setR(std::int32_t&& r) { r_ = std::move(r); }
	std::int32_t getG() const { return cfill_->getG(); }
	void setG(std::int32_t&& g) { g_ = std::move(g); }
	std::int32_t getB() const { return cfill_->getB(); }
	void setB(std::int32_t&& b) { b_ = std::move(b); }
	std::int32_t getA() const { return cfill_->getA(); }
	void setA(std::int32_t&& a) { a_ = std::move(a); }
private:
	Guacamole::Cfill::Reader* cfill_;
	std::uint8_t mask_;
	std::int32_t layer_;
	std::int32_t r_;
	std::int32_t g_;
	std::int32_t b_;
	std::int32_t a_;
};
struct CopyWrapper {
	CopyWrapper(Guacamole::Copy::Reader* copy=nullptr) : copy_(copy) {}
	std::int32_t getSrcLayer() const { return copy_->getSrcLayer(); }
	void setSrcLayer(std::int32_t&& srcLayer) { srcLayer_ = std::move(srcLayer); }
	std::int32_t getSrcX() const { return copy_->getSrcX(); }
	void setSrcX(std::int32_t&& srcX) { srcX_ = std::move(srcX); }
	std::int32_t getSrcY() const { return copy_->getSrcY(); }
	void setSrcY(std::int32_t&& srcY) { srcY_ = std::move(srcY); }
	std::int32_t getSrcWidth() const { return copy_->getSrcWidth(); }
	void setSrcWidth(std::int32_t&& srcWidth) { srcWidth_ = std::move(srcWidth); }
	std::int32_t getSrcHeight() const { return copy_->getSrcHeight(); }
	void setSrcHeight(std::int32_t&& srcHeight) { srcHeight_ = std::move(srcHeight); }
	std::uint8_t getMask() const { return copy_->getMask(); }
	void setMask(std::uint8_t&& mask) { mask_ = std::move(mask); }
	std::int32_t getDstLayer() const { return copy_->getDstLayer(); }
	void setDstLayer(std::int32_t&& dstLayer) { dstLayer_ = std::move(dstLayer); }
	std::int32_t getDstX() const { return copy_->getDstX(); }
	void setDstX(std::int32_t&& dstX) { dstX_ = std::move(dstX); }
	std::int32_t getDstY() const { return copy_->getDstY(); }
	void setDstY(std::int32_t&& dstY) { dstY_ = std::move(dstY); }
private:
	Guacamole::Copy::Reader* copy_;
	std::int32_t srcLayer_;
	std::int32_t srcX_;
	std::int32_t srcY_;
	std::int32_t srcWidth_;
	std::int32_t srcHeight_;
	std::uint8_t mask_;
	std::int32_t dstLayer_;
	std::int32_t dstX_;
	std::int32_t dstY_;
};
struct CstrokeWrapper {
	CstrokeWrapper(Guacamole::Cstroke::Reader* cstroke=nullptr) : cstroke_(cstroke) {}
	std::uint8_t getMask() const { return cstroke_->getMask(); }
	void setMask(std::uint8_t&& mask) { mask_ = std::move(mask); }
	std::int32_t getLayer() const { return cstroke_->getLayer(); }
	void setLayer(std::int32_t&& layer) { layer_ = std::move(layer); }
	std::uint8_t getCap() const { return cstroke_->getCap(); }
	void setCap(std::uint8_t&& cap) { cap_ = std::move(cap); }
	std::uint8_t getJoin() const { return cstroke_->getJoin(); }
	void setJoin(std::uint8_t&& join) { join_ = std::move(join); }
	std::int32_t getThickness() const { return cstroke_->getThickness(); }
	void setThickness(std::int32_t&& thickness) { thickness_ = std::move(thickness); }
	std::int32_t getR() const { return cstroke_->getR(); }
	void setR(std::int32_t&& r) { r_ = std::move(r); }
	std::int32_t getG() const { return cstroke_->getG(); }
	void setG(std::int32_t&& g) { g_ = std::move(g); }
	std::int32_t getB() const { return cstroke_->getB(); }
	void setB(std::int32_t&& b) { b_ = std::move(b); }
	std::int32_t getA() const { return cstroke_->getA(); }
	void setA(std::int32_t&& a) { a_ = std::move(a); }
private:
	Guacamole::Cstroke::Reader* cstroke_;
	std::uint8_t mask_;
	std::int32_t layer_;
	std::uint8_t cap_;
	std::uint8_t join_;
	std::int32_t thickness_;
	std::int32_t r_;
	std::int32_t g_;
	std::int32_t b_;
	std::int32_t a_;
};
struct CursorWrapper {
	CursorWrapper(Guacamole::Cursor::Reader* cursor=nullptr) : cursor_(cursor) {}
	std::int32_t getX() const { return cursor_->getX(); }
	void setX(std::int32_t&& x) { x_ = std::move(x); }
	std::int32_t getY() const { return cursor_->getY(); }
	void setY(std::int32_t&& y) { y_ = std::move(y); }
	std::int32_t getSrcLayer() const { return cursor_->getSrcLayer(); }
	void setSrcLayer(std::int32_t&& srcLayer) { srcLayer_ = std::move(srcLayer); }
	std::int32_t getSrcX() const { return cursor_->getSrcX(); }
	void setSrcX(std::int32_t&& srcX) { srcX_ = std::move(srcX); }
	std::int32_t getSrcY() const { return cursor_->getSrcY(); }
	void setSrcY(std::int32_t&& srcY) { srcY_ = std::move(srcY); }
	std::int32_t getSrcWidth() const { return cursor_->getSrcWidth(); }
	void setSrcWidth(std::int32_t&& srcWidth) { srcWidth_ = std::move(srcWidth); }
	std::int32_t getSrcHeight() const { return cursor_->getSrcHeight(); }
	void setSrcHeight(std::int32_t&& srcHeight) { srcHeight_ = std::move(srcHeight); }
private:
	Guacamole::Cursor::Reader* cursor_;
	std::int32_t x_;
	std::int32_t y_;
	std::int32_t srcLayer_;
	std::int32_t srcX_;
	std::int32_t srcY_;
	std::int32_t srcWidth_;
	std::int32_t srcHeight_;
};
struct CurveWrapper {
	CurveWrapper(Guacamole::Curve::Reader* curve=nullptr) : curve_(curve) {}
	std::int32_t getLayer() const { return curve_->getLayer(); }
	void setLayer(std::int32_t&& layer) { layer_ = std::move(layer); }
	std::int32_t getCp1x() const { return curve_->getCp1x(); }
	void setCp1x(std::int32_t&& cp1x) { cp1x_ = std::move(cp1x); }
	std::int32_t getCp1y() const { return curve_->getCp1y(); }
	void setCp1y(std::int32_t&& cp1y) { cp1y_ = std::move(cp1y); }
	std::int32_t getCp2x() const { return curve_->getCp2x(); }
	void setCp2x(std::int32_t&& cp2x) { cp2x_ = std::move(cp2x); }
	std::int32_t getCp2y() const { return curve_->getCp2y(); }
	void setCp2y(std::int32_t&& cp2y) { cp2y_ = std::move(cp2y); }
	std::int32_t getX() const { return curve_->getX(); }
	void setX(std::int32_t&& x) { x_ = std::move(x); }
	std::int32_t getY() const { return curve_->getY(); }
	void setY(std::int32_t&& y) { y_ = std::move(y); }
private:
	Guacamole::Curve::Reader* curve_;
	std::int32_t layer_;
	std::int32_t cp1x_;
	std::int32_t cp1y_;
	std::int32_t cp2x_;
	std::int32_t cp2y_;
	std::int32_t x_;
	std::int32_t y_;
};
struct DistortWrapper {
	DistortWrapper(Guacamole::Distort::Reader* distort=nullptr) : distort_(distort) {}
	std::int32_t getLayer() const { return distort_->getLayer(); }
	void setLayer(std::int32_t&& layer) { layer_ = std::move(layer); }
	double getA() const { return distort_->getA(); }
	void setA(double&& a) { a_ = std::move(a); }
	double getB() const { return distort_->getB(); }
	void setB(double&& b) { b_ = std::move(b); }
	double getC() const { return distort_->getC(); }
	void setC(double&& c) { c_ = std::move(c); }
	double getD() const { return distort_->getD(); }
	void setD(double&& d) { d_ = std::move(d); }
	double getE() const { return distort_->getE(); }
	void setE(double&& e) { e_ = std::move(e); }
	double getF() const { return distort_->getF(); }
	void setF(double&& f) { f_ = std::move(f); }
private:
	Guacamole::Distort::Reader* distort_;
	std::int32_t layer_;
	double a_;
	double b_;
	double c_;
	double d_;
	double e_;
	double f_;
};
struct LfillWrapper {
	LfillWrapper(Guacamole::Lfill::Reader* lfill=nullptr) : lfill_(lfill) {}
	std::uint8_t getMask() const { return lfill_->getMask(); }
	void setMask(std::uint8_t&& mask) { mask_ = std::move(mask); }
	std::int32_t getLayer() const { return lfill_->getLayer(); }
	void setLayer(std::int32_t&& layer) { layer_ = std::move(layer); }
	std::int32_t getSrcLayer() const { return lfill_->getSrcLayer(); }
	void setSrcLayer(std::int32_t&& srcLayer) { srcLayer_ = std::move(srcLayer); }
private:
	Guacamole::Lfill::Reader* lfill_;
	std::uint8_t mask_;
	std::int32_t layer_;
	std::int32_t srcLayer_;
};
struct LineWrapper {
	LineWrapper(Guacamole::Line::Reader* line=nullptr) : line_(line) {}
	std::int32_t getLayer() const { return line_->getLayer(); }
	void setLayer(std::int32_t&& layer) { layer_ = std::move(layer); }
	std::int32_t getX() const { return line_->getX(); }
	void setX(std::int32_t&& x) { x_ = std::move(x); }
	std::int32_t getY() const { return line_->getY(); }
	void setY(std::int32_t&& y) { y_ = std::move(y); }
private:
	Guacamole::Line::Reader* line_;
	std::int32_t layer_;
	std::int32_t x_;
	std::int32_t y_;
};
struct LstrokeWrapper {
	LstrokeWrapper(Guacamole::Lstroke::Reader* lstroke=nullptr) : lstroke_(lstroke) {}
	std::uint8_t getMask() const { return lstroke_->getMask(); }
	void setMask(std::uint8_t&& mask) { mask_ = std::move(mask); }
	std::int32_t getLayer() const { return lstroke_->getLayer(); }
	void setLayer(std::int32_t&& layer) { layer_ = std::move(layer); }
	Guacamole::Lstroke::LineCap getCap() const { return lstroke_->getCap(); }
	void setCap(std::uint8_t&& cap) { cap_ = std::move(cap); }
	std::uint8_t getJoin() const { return lstroke_->getJoin(); }
	void setJoin(std::uint8_t&& join) { join_ = std::move(join); }
	std::int32_t getThickness() const { return lstroke_->getThickness(); }
	void setThickness(std::int32_t&& thickness) { thickness_ = std::move(thickness); }
	std::int32_t getSrcLayer() const { return lstroke_->getSrcLayer(); }
	void setSrcLayer(std::int32_t&& srcLayer) { srcLayer_ = std::move(srcLayer); }
private:
	Guacamole::Lstroke::Reader* lstroke_;
	std::uint8_t mask_;
	std::int32_t layer_;
	std::uint8_t cap_;
	std::uint8_t join_;
	std::int32_t thickness_;
	std::int32_t srcLayer_;
};
struct MoveWrapper {
	MoveWrapper(Guacamole::Move::Reader* move=nullptr) : move_(move) {}
	std::int32_t getLayer() const { return move_->getLayer(); }
	void setLayer(std::int32_t&& layer) { layer_ = std::move(layer); }
	std::int32_t getParent() const { return move_->getParent(); }
	void setParent(std::int32_t&& parent) { parent_ = std::move(parent); }
	std::int32_t getX() const { return move_->getX(); }
	void setX(std::int32_t&& x) { x_ = std::move(x); }
	std::int32_t getY() const { return move_->getY(); }
	void setY(std::int32_t&& y) { y_ = std::move(y); }
	std::int32_t getZ() const { return move_->getZ(); }
	void setZ(std::int32_t&& z) { z_ = std::move(z); }
private:
	Guacamole::Move::Reader* move_;
	std::int32_t layer_;
	std::int32_t parent_;
	std::int32_t x_;
	std::int32_t y_;
	std::int32_t z_;
};
struct RectWrapper {
	RectWrapper(Guacamole::Rect::Reader* rect=nullptr) : rect_(rect) {}
	std::int32_t getLayer() const { return rect_->getLayer(); }
	void setLayer(std::int32_t&& layer) { layer_ = std::move(layer); }
	std::int32_t getX() const { return rect_->getX(); }
	void setX(std::int32_t&& x) { x_ = std::move(x); }
	std::int32_t getY() const { return rect_->getY(); }
	void setY(std::int32_t&& y) { y_ = std::move(y); }
	std::int32_t getWidth() const { return rect_->getWidth(); }
	void setWidth(std::int32_t&& width) { width_ = std::move(width); }
	std::int32_t getHeight() const { return rect_->getHeight(); }
	void setHeight(std::int32_t&& height) { height_ = std::move(height); }
private:
	Guacamole::Rect::Reader* rect_;
	std::int32_t layer_;
	std::int32_t x_;
	std::int32_t y_;
	std::int32_t width_;
	std::int32_t height_;
};
struct SetWrapper {
	SetWrapper(Guacamole::Set::Reader* set=nullptr) : set_(set) {}
	std::int32_t getLayer() const { return set_->getLayer(); }
	void setLayer(std::int32_t&& layer) { layer_ = std::move(layer); }
	std::string getProperty() const { return set_->getProperty(); }
	void setProperty(std::string&& property) { property_ = std::move(property); }
	std::string getValue() const { return set_->getValue(); }
	void setValue(std::string&& value) { value_ = std::move(value); }
private:
	Guacamole::Set::Reader* set_;
	std::int32_t layer_;
	std::string property_;
	std::string value_;
};
struct ShadeWrapper {
	ShadeWrapper(Guacamole::Shade::Reader* shade=nullptr) : shade_(shade) {}
	std::int32_t getLayer() const { return shade_->getLayer(); }
	void setLayer(std::int32_t&& layer) { layer_ = std::move(layer); }
	std::int32_t getOpacity() const { return shade_->getOpacity(); }
	void setOpacity(std::int32_t&& opacity) { opacity_ = std::move(opacity); }
private:
	Guacamole::Shade::Reader* shade_;
	std::int32_t layer_;
	std::int32_t opacity_;
};
struct LayerSizeWrapper {
	LayerSizeWrapper(Guacamole::LayerSize::Reader* layerSize=nullptr) : layerSize_(layerSize) {}
	std::int32_t getLayer() const { return layerSize_->getLayer(); }
	void setLayer(std::int32_t&& layer) { layer_ = std::move(layer); }
	std::int32_t getWidth() const { return layerSize_->getWidth(); }
	void setWidth(std::int32_t&& width) { width_ = std::move(width); }
	std::int32_t getHeight() const { return layerSize_->getHeight(); }
	void setHeight(std::int32_t&& height) { height_ = std::move(height); }
private:
	Guacamole::LayerSize::Reader* layerSize_;
	std::int32_t layer_;
	std::int32_t width_;
	std::int32_t height_;
};
struct StartWrapper {
	StartWrapper(Guacamole::Start::Reader* start=nullptr) : start_(start) {}
	std::int32_t getLayer() const { return start_->getLayer(); }
	void setLayer(std::int32_t&& layer) { layer_ = std::move(layer); }
	std::int32_t getX() const { return start_->getX(); }
	void setX(std::int32_t&& x) { x_ = std::move(x); }
	std::int32_t getY() const { return start_->getY(); }
	void setY(std::int32_t&& y) { y_ = std::move(y); }
private:
	Guacamole::Start::Reader* start_;
	std::int32_t layer_;
	std::int32_t x_;
	std::int32_t y_;
};
struct TransferWrapper {
	TransferWrapper(Guacamole::Transfer::Reader* transfer=nullptr) : transfer_(transfer) {}
	std::int32_t getSrcLayer() const { return transfer_->getSrcLayer(); }
	void setSrcLayer(std::int32_t&& srcLayer) { srcLayer_ = std::move(srcLayer); }
	std::int32_t getSrcX() const { return transfer_->getSrcX(); }
	void setSrcX(std::int32_t&& srcX) { srcX_ = std::move(srcX); }
	std::int32_t getSrcY() const { return transfer_->getSrcY(); }
	void setSrcY(std::int32_t&& srcY) { srcY_ = std::move(srcY); }
	std::int32_t getSrcWidth() const { return transfer_->getSrcWidth(); }
	void setSrcWidth(std::int32_t&& srcWidth) { srcWidth_ = std::move(srcWidth); }
	std::int32_t getSrcHeight() const { return transfer_->getSrcHeight(); }
	void setSrcHeight(std::int32_t&& srcHeight) { srcHeight_ = std::move(srcHeight); }
	std::uint8_t getFunction() const { return transfer_->getFunction(); }
	void setFunction(std::uint8_t&& function) { function_ = std::move(function); }
	std::int32_t getDstLayer() const { return transfer_->getDstLayer(); }
	void setDstLayer(std::int32_t&& dstLayer) { dstLayer_ = std::move(dstLayer); }
	std::int32_t getDstX() const { return transfer_->getDstX(); }
	void setDstX(std::int32_t&& dstX) { dstX_ = std::move(dstX); }
	std::int32_t getDstY() const { return transfer_->getDstY(); }
	void setDstY(std::int32_t&& dstY) { dstY_ = std::move(dstY); }
private:
	Guacamole::Transfer::Reader* transfer_;
	std::int32_t srcLayer_;
	std::int32_t srcX_;
	std::int32_t srcY_;
	std::int32_t srcWidth_;
	std::int32_t srcHeight_;
	std::uint8_t function_;
	std::int32_t dstLayer_;
	std::int32_t dstX_;
	std::int32_t dstY_;
};
struct TransformWrapper {
	TransformWrapper(Guacamole::Transform::Reader* transform=nullptr) : transform_(transform) {}
	std::int32_t getLayer() const { return transform_->getLayer(); }
	void setLayer(std::int32_t&& layer) { layer_ = std::move(layer); }
	double getA() const { return transform_->getA(); }
	void setA(double&& a) { a_ = std::move(a); }
	double getB() const { return transform_->getB(); }
	void setB(double&& b) { b_ = std::move(b); }
	double getC() const { return transform_->getC(); }
	void setC(double&& c) { c_ = std::move(c); }
	double getD() const { return transform_->getD(); }
	void setD(double&& d) { d_ = std::move(d); }
	double getE() const { return transform_->getE(); }
	void setE(double&& e) { e_ = std::move(e); }
	double getF() const { return transform_->getF(); }
	void setF(double&& f) { f_ = std::move(f); }
private:
	Guacamole::Transform::Reader* transform_;
	std::int32_t layer_;
	double a_;
	double b_;
	double c_;
	double d_;
	double e_;
	double f_;
};
struct AckWrapper {
	AckWrapper(Guacamole::Ack::Reader* ack=nullptr) : ack_(ack) {}
	std::int32_t getStream() const { return ack_->getStream(); }
	void setStream(std::int32_t&& stream) { stream_ = std::move(stream); }
	std::string getMessage() const { return ack_->getMessage(); }
	void setMessage(std::string&& message) { message_ = std::move(message); }
	std::int32_t getStatus() const { return ack_->getStatus(); }
	void setStatus(std::int32_t&& status) { status_ = std::move(status); }
private:
	Guacamole::Ack::Reader* ack_;
	std::int32_t stream_;
	std::string message_;
	std::int32_t status_;
};
struct AudioWrapper {
	AudioWrapper(Guacamole::Audio::Reader* audio=nullptr) : audio_(audio) {}
	std::int32_t getStream() const { return audio_->getStream(); }
	void setStream(std::int32_t&& stream) { stream_ = std::move(stream); }
	std::string getMimetype() const { return audio_->getMimetype(); }
	void setMimetype(std::string&& mimetype) { mimetype_ = std::move(mimetype); }
private:
	Guacamole::Audio::Reader* audio_;
	std::int32_t stream_;
	std::string mimetype_;
};
struct BlobWrapper {
	BlobWrapper(Guacamole::Blob::Reader* blob=nullptr) : blob_(blob) {}
	std::int32_t getStream() const { return blob_->getStream(); }
	void setStream(std::int32_t&& stream) { stream_ = std::move(stream); }
	emscripten::val getData() const {
		const auto data = blob_->getData();
		return emscripten::val(emscripten::typed_memory_view(data.size(), data.begin()));
	}
	void setData(std::string&& data) { data_ = std::move(data); }
private:
	Guacamole::Blob::Reader* blob_;
	std::int32_t stream_;
	std::string data_;
};
struct ClipboardWrapper {
	ClipboardWrapper(Guacamole::Clipboard::Reader* clipboard=nullptr) : clipboard_(clipboard) {}
	std::int32_t getStream() const { return clipboard_->getStream(); }
	void setStream(std::int32_t&& stream) { stream_ = std::move(stream); }
	std::string getMimetype() const { return clipboard_->getMimetype(); }
	void setMimetype(std::string&& mimetype) { mimetype_ = std::move(mimetype); }
private:
	Guacamole::Clipboard::Reader* clipboard_;
	std::int32_t stream_;
	std::string mimetype_;
};
struct FileWrapper {
	FileWrapper(Guacamole::File::Reader* file=nullptr) : file_(file) {}
	std::int32_t getStream() const { return file_->getStream(); }
	void setStream(std::int32_t&& stream) { stream_ = std::move(stream); }
	std::string getMimetype() const { return file_->getMimetype(); }
	void setMimetype(std::string&& mimetype) { mimetype_ = std::move(mimetype); }
	std::string getFilename() const { return file_->getFilename(); }
	void setFilename(std::string&& filename) { filename_ = std::move(filename); }
private:
	Guacamole::File::Reader* file_;
	std::int32_t stream_;
	std::string mimetype_;
	std::string filename_;
};
struct ImgWrapper {
	ImgWrapper(Guacamole::Img::Reader* img=nullptr) : img_(img) {}
	std::int32_t getStream() const { return img_->getStream(); }
	void setStream(std::int32_t&& stream) { stream_ = std::move(stream); }
	std::uint8_t getMode() const { return img_->getMode(); }
	void setMode(std::uint8_t&& mode) { mode_ = std::move(mode); }
	std::int32_t getLayer() const { return img_->getLayer(); }
	void setLayer(std::int32_t&& layer) { layer_ = std::move(layer); }
	std::string getMimetype() const { return img_->getMimetype(); }
	void setMimetype(std::string&& mimetype) { mimetype_ = std::move(mimetype); }
	std::int32_t getX() const { return img_->getX(); }
	void setX(std::int32_t&& x) { x_ = std::move(x); }
	std::int32_t getY() const { return img_->getY(); }
	void setY(std::int32_t&& y) { y_ = std::move(y); }
private:
	Guacamole::Img::Reader* img_;
	std::int32_t stream_;
	std::uint8_t mode_;
	std::int32_t layer_;
	std::string mimetype_;
	std::int32_t x_;
	std::int32_t y_;
};
struct NestWrapper {
	NestWrapper(Guacamole::Nest::Reader* nest=nullptr) : nest_(nest) {}
	std::int32_t getIndex() const { return nest_->getIndex(); }
	void setIndex(std::int32_t&& index) { index_ = std::move(index); }
	std::string getData() const { return std::string(reinterpret_cast<const char*>(nest_->getData().begin()), nest_->getData().size()); }
	void setData(std::string&& data) { data_ = std::move(data); }
private:
	Guacamole::Nest::Reader* nest_;
	std::int32_t index_;
	std::string data_;
};
struct PipeWrapper {
	PipeWrapper(Guacamole::Pipe::Reader* pipe=nullptr) : pipe_(pipe) {}
	std::int32_t getStream() const { return pipe_->getStream(); }
	void setStream(std::int32_t&& stream) { stream_ = std::move(stream); }
	std::string getMimetype() const { return pipe_->getMimetype(); }
	void setMimetype(std::string&& mimetype) { mimetype_ = std::move(mimetype); }
	std::string getName() const { return pipe_->getName(); }
	void setName(std::string&& name) { name_ = std::move(name); }
private:
	Guacamole::Pipe::Reader* pipe_;
	std::int32_t stream_;
	std::string mimetype_;
	std::string name_;
};
struct VideoWrapper {
	VideoWrapper(Guacamole::Video::Reader* video=nullptr) : video_(video) {}
	std::int32_t getStream() const { return video_->getStream(); }
	void setStream(std::int32_t&& stream) { stream_ = std::move(stream); }
	std::int32_t getLayer() const { return video_->getLayer(); }
	void setLayer(std::int32_t&& layer) { layer_ = std::move(layer); }
	std::string getMimetype() const { return video_->getMimetype(); }
	void setMimetype(std::string&& mimetype) { mimetype_ = std::move(mimetype); }
private:
	Guacamole::Video::Reader* video_;
	std::int32_t stream_;
	std::int32_t layer_;
	std::string mimetype_;
};
struct BodyWrapper {
	BodyWrapper(Guacamole::Body::Reader* body=nullptr) : body_(body) {}
	std::int32_t getObject() const { return body_->getObject(); }
	void setObject(std::int32_t&& object) { object_ = std::move(object); }
	std::int32_t getStream() const { return body_->getStream(); }
	void setStream(std::int32_t&& stream) { stream_ = std::move(stream); }
	std::string getMimetype() const { return body_->getMimetype(); }
	void setMimetype(std::string&& mimetype) { mimetype_ = std::move(mimetype); }
	std::string getName() const { return body_->getName(); }
	void setName(std::string&& name) { name_ = std::move(name); }
private:
	Guacamole::Body::Reader* body_;
	std::int32_t object_;
	std::int32_t stream_;
	std::string mimetype_;
	std::string name_;
};
struct FilesystemWrapper {
	FilesystemWrapper(Guacamole::Filesystem::Reader* filesystem=nullptr) : filesystem_(filesystem) {}
	std::int32_t getObject() const { return filesystem_->getObject(); }
	void setObject(std::int32_t&& object) { object_ = std::move(object); }
	std::string getName() const { return filesystem_->getName(); }
	void setName(std::string&& name) { name_ = std::move(name); }
private:
	Guacamole::Filesystem::Reader* filesystem_;
	std::int32_t object_;
	std::string name_;
};
struct ErrorWrapper {
	ErrorWrapper(Guacamole::Error::Reader* error=nullptr) : error_(error) {}
	std::string getText() const { return error_->getText(); }
	void setText(std::string&& text) { text_ = std::move(text); }
	std::int32_t getStatus() const { return error_->getStatus(); }
	void setStatus(std::int32_t&& status) { status_ = std::move(status); }
private:
	Guacamole::Error::Reader* error_;
	std::string text_;
	std::int32_t status_;
};
struct ServerMouseWrapper {
	ServerMouseWrapper(Guacamole::ServerMouse::Reader* mouse=nullptr) : mouse_(mouse) {}
	std::int32_t getX() const { return mouse_->getX(); }
	void setX(std::int32_t&& x) { x_ = std::move(x); }
	std::int32_t getY() const { return mouse_->getY(); }
	void setY(std::int32_t&& y) { y_ = std::move(y); }
	std::int32_t getButtonMask() const { return mouse_->getButtonMask(); }
	void setButtonMask(std::int32_t&& button_mask) { button_mask_ = std::move(button_mask); }
	double getTimestamp() const { return mouse_->getTimestamp(); }
	void setTimestamp(double&& timestamp) { timestamp_ = std::move(timestamp); }
private:
	Guacamole::ServerMouse::Reader* mouse_;
	std::int32_t x_;
	std::int32_t y_;
	std::int32_t button_mask_;
	double timestamp_;
};
struct ServerKeyWrapper {
	ServerKeyWrapper(Guacamole::ServerKey::Reader* key=nullptr) : key_(key) {}
	std::int32_t getKeysym() const { return key_->getKeysym(); }
	void setKeysym(std::int32_t&& keysym) { keysym_ = std::move(keysym); }
	std::int32_t getPressed() const { return key_->getPressed(); }
	void setPressed(std::int32_t&& pressed) { pressed_ = std::move(pressed); }
	double getTimestamp() const { return key_->getTimestamp(); }
	void setTimestamp(double&& timestamp) { timestamp_ = std::move(timestamp); }
private:
	Guacamole::ServerKey::Reader* key_;
	std::int32_t keysym_;
	std::int32_t pressed_;
	double timestamp_;
};
struct PointWrapper {
	PointWrapper(Guacamole::Point::Reader* point=nullptr) : point_(point) {}
	std::int32_t getX() const { return point_->getX(); }
	void setX(std::int32_t&& x) { x_ = std::move(x); }
	std::int32_t getY() const { return point_->getY(); }
	void setY(std::int32_t&& y) { y_ = std::move(y); }
private:
	Guacamole::Point::Reader* point_;
	std::int32_t x_;
	std::int32_t y_;
};

EMSCRIPTEN_BINDINGS(guac_wrappers) {

	emscripten::value_object<ArcWrapper>("Arc")
		.field("layer", &ArcWrapper::getLayer, &ArcWrapper::setLayer)
		.field("x", &ArcWrapper::getX, &ArcWrapper::setX)
		.field("y", &ArcWrapper::getY, &ArcWrapper::setY)
		.field("radius", &ArcWrapper::getRadius, &ArcWrapper::setRadius)
		.field("start", &ArcWrapper::getStart, &ArcWrapper::setStart)
		.field("end", &ArcWrapper::getEnd, &ArcWrapper::setEnd)
		.field("negative", &ArcWrapper::getNegative, &ArcWrapper::setNegative)
		;
	emscripten::value_object<CfillWrapper>("Cfill")
		.field("mask", &CfillWrapper::getMask, &CfillWrapper::setMask)
		.field("layer", &CfillWrapper::getLayer, &CfillWrapper::setLayer)
		.field("r", &CfillWrapper::getR, &CfillWrapper::setR)
		.field("g", &CfillWrapper::getG, &CfillWrapper::setG)
		.field("b", &CfillWrapper::getB, &CfillWrapper::setB)
		.field("a", &CfillWrapper::getA, &CfillWrapper::setA)
		;
	emscripten::value_object<CopyWrapper>("Copy")
		.field("srcLayer", &CopyWrapper::getSrcLayer, &CopyWrapper::setSrcLayer)
		.field("srcX", &CopyWrapper::getSrcX, &CopyWrapper::setSrcX)
		.field("srcY", &CopyWrapper::getSrcY, &CopyWrapper::setSrcY)
		.field("srcWidth", &CopyWrapper::getSrcWidth, &CopyWrapper::setSrcWidth)
		.field("srcHeight", &CopyWrapper::getSrcHeight, &CopyWrapper::setSrcHeight)
		.field("mask", &CopyWrapper::getMask, &CopyWrapper::setMask)
		.field("dstLayer", &CopyWrapper::getDstLayer, &CopyWrapper::setDstLayer)
		.field("dstX", &CopyWrapper::getDstX, &CopyWrapper::setDstX)
		.field("dstY", &CopyWrapper::getDstY, &CopyWrapper::setDstY)
		;
	emscripten::value_object<CstrokeWrapper>("Cstroke")
		.field("mask", &CstrokeWrapper::getMask, &CstrokeWrapper::setMask)
		.field("layer", &CstrokeWrapper::getLayer, &CstrokeWrapper::setLayer)
		.field("cap", &CstrokeWrapper::getCap, &CstrokeWrapper::setCap)
		.field("join", &CstrokeWrapper::getJoin, &CstrokeWrapper::setJoin)
		.field("thickness", &CstrokeWrapper::getThickness, &CstrokeWrapper::setThickness)
		.field("r", &CstrokeWrapper::getR, &CstrokeWrapper::setR)
		.field("g", &CstrokeWrapper::getG, &CstrokeWrapper::setG)
		.field("b", &CstrokeWrapper::getB, &CstrokeWrapper::setB)
		.field("a", &CstrokeWrapper::getA, &CstrokeWrapper::setA)
		;
	emscripten::value_object<CursorWrapper>("Cursor")
		.field("x", &CursorWrapper::getX, &CursorWrapper::setX)
		.field("y", &CursorWrapper::getY, &CursorWrapper::setY)
		.field("srcLayer", &CursorWrapper::getSrcLayer, &CursorWrapper::setSrcLayer)
		.field("srcX", &CursorWrapper::getSrcX, &CursorWrapper::setSrcX)
		.field("srcY", &CursorWrapper::getSrcY, &CursorWrapper::setSrcY)
		.field("srcWidth", &CursorWrapper::getSrcWidth, &CursorWrapper::setSrcWidth)
		.field("srcHeight", &CursorWrapper::getSrcHeight, &CursorWrapper::setSrcHeight)
		;
	emscripten::value_object<CurveWrapper>("Curve")
		.field("layer", &CurveWrapper::getLayer, &CurveWrapper::setLayer)
		.field("cp1x", &CurveWrapper::getCp1x, &CurveWrapper::setCp1x)
		.field("cp1y", &CurveWrapper::getCp1y, &CurveWrapper::setCp1y)
		.field("cp2x", &CurveWrapper::getCp2x, &CurveWrapper::setCp2x)
		.field("cp2y", &CurveWrapper::getCp2y, &CurveWrapper::setCp2y)
		.field("x", &CurveWrapper::getX, &CurveWrapper::setX)
		.field("y", &CurveWrapper::getY, &CurveWrapper::setY)
		;
	emscripten::value_object<DistortWrapper>("Distort")
		.field("layer", &DistortWrapper::getLayer, &DistortWrapper::setLayer)
		.field("a", &DistortWrapper::getA, &DistortWrapper::setA)
		.field("b", &DistortWrapper::getB, &DistortWrapper::setB)
		.field("c", &DistortWrapper::getC, &DistortWrapper::setC)
		.field("d", &DistortWrapper::getD, &DistortWrapper::setD)
		.field("e", &DistortWrapper::getE, &DistortWrapper::setE)
		.field("f", &DistortWrapper::getF, &DistortWrapper::setF)
		;
	emscripten::value_object<LfillWrapper>("Lfill")
		.field("mask", &LfillWrapper::getMask, &LfillWrapper::setMask)
		.field("layer", &LfillWrapper::getLayer, &LfillWrapper::setLayer)
		.field("srcLayer", &LfillWrapper::getSrcLayer, &LfillWrapper::setSrcLayer)
		;
	emscripten::value_object<LineWrapper>("Line")
		.field("layer", &LineWrapper::getLayer, &LineWrapper::setLayer)
		.field("x", &LineWrapper::getX, &LineWrapper::setX)
		.field("y", &LineWrapper::getY, &LineWrapper::setY)
		;
	emscripten::value_object<LstrokeWrapper>("Lstroke")
		.field("mask", &LstrokeWrapper::getMask, &LstrokeWrapper::setMask)
		.field("layer", &LstrokeWrapper::getLayer, &LstrokeWrapper::setLayer)
		.field("cap", &LstrokeWrapper::getCap, &LstrokeWrapper::setCap)
		.field("join", &LstrokeWrapper::getJoin, &LstrokeWrapper::setJoin)
		.field("thickness", &LstrokeWrapper::getThickness, &LstrokeWrapper::setThickness)
		.field("srcLayer", &LstrokeWrapper::getSrcLayer, &LstrokeWrapper::setSrcLayer)
		;
	emscripten::value_object<MoveWrapper>("Move")
		.field("layer", &MoveWrapper::getLayer, &MoveWrapper::setLayer)
		.field("parent", &MoveWrapper::getParent, &MoveWrapper::setParent)
		.field("x", &MoveWrapper::getX, &MoveWrapper::setX)
		.field("y", &MoveWrapper::getY, &MoveWrapper::setY)
		.field("z", &MoveWrapper::getZ, &MoveWrapper::setZ)
		;
	emscripten::value_object<RectWrapper>("Rect")
		.field("layer", &RectWrapper::getLayer, &RectWrapper::setLayer)
		.field("x", &RectWrapper::getX, &RectWrapper::setX)
		.field("y", &RectWrapper::getY, &RectWrapper::setY)
		.field("width", &RectWrapper::getWidth, &RectWrapper::setWidth)
		.field("height", &RectWrapper::getHeight, &RectWrapper::setHeight)
		;
	emscripten::value_object<SetWrapper>("Set")
		.field("layer", &SetWrapper::getLayer, &SetWrapper::setLayer)
		.field("property", &SetWrapper::getProperty, &SetWrapper::setProperty)
		.field("value", &SetWrapper::getValue, &SetWrapper::setValue)
		;
	emscripten::value_object<ShadeWrapper>("Shade")
		.field("layer", &ShadeWrapper::getLayer, &ShadeWrapper::setLayer)
		.field("opacity", &ShadeWrapper::getOpacity, &ShadeWrapper::setOpacity)
		;
	emscripten::value_object<LayerSizeWrapper>("LayerSize")
		.field("layer", &LayerSizeWrapper::getLayer, &LayerSizeWrapper::setLayer)
		.field("width", &LayerSizeWrapper::getWidth, &LayerSizeWrapper::setWidth)
		.field("height", &LayerSizeWrapper::getHeight, &LayerSizeWrapper::setHeight)
		;
	emscripten::value_object<StartWrapper>("Start")
		.field("layer", &StartWrapper::getLayer, &StartWrapper::setLayer)
		.field("x", &StartWrapper::getX, &StartWrapper::setX)
		.field("y", &StartWrapper::getY, &StartWrapper::setY)
		;
	emscripten::value_object<TransferWrapper>("Transfer")
		.field("srcLayer", &TransferWrapper::getSrcLayer, &TransferWrapper::setSrcLayer)
		.field("srcX", &TransferWrapper::getSrcX, &TransferWrapper::setSrcX)
		.field("srcY", &TransferWrapper::getSrcY, &TransferWrapper::setSrcY)
		.field("srcWidth", &TransferWrapper::getSrcWidth, &TransferWrapper::setSrcWidth)
		.field("srcHeight", &TransferWrapper::getSrcHeight, &TransferWrapper::setSrcHeight)
		.field("function", &TransferWrapper::getFunction, &TransferWrapper::setFunction)
		.field("dstLayer", &TransferWrapper::getDstLayer, &TransferWrapper::setDstLayer)
		.field("dstX", &TransferWrapper::getDstX, &TransferWrapper::setDstX)
		.field("dstY", &TransferWrapper::getDstY, &TransferWrapper::setDstY)
		;
	emscripten::value_object<TransformWrapper>("Transform")
		.field("layer", &TransformWrapper::getLayer, &TransformWrapper::setLayer)
		.field("a", &TransformWrapper::getA, &TransformWrapper::setA)
		.field("b", &TransformWrapper::getB, &TransformWrapper::setB)
		.field("c", &TransformWrapper::getC, &TransformWrapper::setC)
		.field("d", &TransformWrapper::getD, &TransformWrapper::setD)
		.field("e", &TransformWrapper::getE, &TransformWrapper::setE)
		.field("f", &TransformWrapper::getF, &TransformWrapper::setF)
		;
	emscripten::value_object<AckWrapper>("Ack")
		.field("stream", &AckWrapper::getStream, &AckWrapper::setStream)
		.field("message", &AckWrapper::getMessage, &AckWrapper::setMessage)
		.field("status", &AckWrapper::getStatus, &AckWrapper::setStatus)
		;
	emscripten::value_object<AudioWrapper>("Audio")
		.field("stream", &AudioWrapper::getStream, &AudioWrapper::setStream)
		.field("mimetype", &AudioWrapper::getMimetype, &AudioWrapper::setMimetype)
		;
	emscripten::value_object<BlobWrapper>("Blob")
		.field("stream", &BlobWrapper::getStream, &BlobWrapper::setStream)
		.field("data", &BlobWrapper::getData, &BlobWrapper::setData)
		;
	emscripten::value_object<ClipboardWrapper>("Clipboard")
		.field("stream", &ClipboardWrapper::getStream, &ClipboardWrapper::setStream)
		.field("mimetype", &ClipboardWrapper::getMimetype, &ClipboardWrapper::setMimetype)
		;
	emscripten::value_object<FileWrapper>("File")
		.field("stream", &FileWrapper::getStream, &FileWrapper::setStream)
		.field("mimetype", &FileWrapper::getMimetype, &FileWrapper::setMimetype)
		.field("filename", &FileWrapper::getFilename, &FileWrapper::setFilename)
		;
	emscripten::value_object<ImgWrapper>("Img")
		.field("stream", &ImgWrapper::getStream, &ImgWrapper::setStream)
		.field("mode", &ImgWrapper::getMode, &ImgWrapper::setMode)
		.field("layer", &ImgWrapper::getLayer, &ImgWrapper::setLayer)
		.field("mimetype", &ImgWrapper::getMimetype, &ImgWrapper::setMimetype)
		.field("x", &ImgWrapper::getX, &ImgWrapper::setX)
		.field("y", &ImgWrapper::getY, &ImgWrapper::setY)
		;
	emscripten::value_object<NestWrapper>("Nest")
		.field("index", &NestWrapper::getIndex, &NestWrapper::setIndex)
		.field("data", &NestWrapper::getData, &NestWrapper::setData)
		;
	emscripten::value_object<PipeWrapper>("Pipe")
		.field("stream", &PipeWrapper::getStream, &PipeWrapper::setStream)
		.field("mimetype", &PipeWrapper::getMimetype, &PipeWrapper::setMimetype)
		.field("name", &PipeWrapper::getName, &PipeWrapper::setName)
		;
	emscripten::value_object<VideoWrapper>("Video")
		.field("stream", &VideoWrapper::getStream, &VideoWrapper::setStream)
		.field("layer", &VideoWrapper::getLayer, &VideoWrapper::setLayer)
		.field("mimetype", &VideoWrapper::getMimetype, &VideoWrapper::setMimetype)
		;
	emscripten::value_object<BodyWrapper>("Body")
		.field("object", &BodyWrapper::getObject, &BodyWrapper::setObject)
		.field("stream", &BodyWrapper::getStream, &BodyWrapper::setStream)
		.field("mimetype", &BodyWrapper::getMimetype, &BodyWrapper::setMimetype)
		.field("name", &BodyWrapper::getName, &BodyWrapper::setName)
		;
	emscripten::value_object<FilesystemWrapper>("Filesystem")
		.field("object", &FilesystemWrapper::getObject, &FilesystemWrapper::setObject)
		.field("name", &FilesystemWrapper::getName, &FilesystemWrapper::setName)
		;
	emscripten::value_object<ErrorWrapper>("Error")
		.field("text", &ErrorWrapper::getText, &ErrorWrapper::setText)
		.field("status", &ErrorWrapper::getStatus, &ErrorWrapper::setStatus)
		;
	emscripten::value_object<ServerMouseWrapper>("ServerMouse")
		.field("x", &ServerMouseWrapper::getX, &ServerMouseWrapper::setX)
		.field("y", &ServerMouseWrapper::getY, &ServerMouseWrapper::setY)
		.field("buttonMask", &ServerMouseWrapper::getButtonMask, &ServerMouseWrapper::setButtonMask)
		.field("timestamp", &ServerMouseWrapper::getTimestamp, &ServerMouseWrapper::setTimestamp)
		;
	emscripten::value_object<ServerKeyWrapper>("ServerKey")
		.field("keysym", &ServerKeyWrapper::getKeysym, &ServerKeyWrapper::setKeysym)
		.field("pressed", &ServerKeyWrapper::getPressed, &ServerKeyWrapper::setPressed)
		.field("timestamp", &ServerKeyWrapper::getTimestamp, &ServerKeyWrapper::setTimestamp)
		;
	emscripten::value_object<PointWrapper>("Point")
		.field("x", &PointWrapper::getX, &PointWrapper::setX)
		.field("y", &PointWrapper::getY, &PointWrapper::setY)
		;
}
