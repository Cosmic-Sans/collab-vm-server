@0xa2f244af0cefd1ae;
using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("Guacamole");

using Layer = Int32;
using Stream = Int32;
using Status = Int32;
using Object = Int32;

using CompositeMode = UInt8;
using Connect = Void;
using Image = Void;
using LineJoinStyle = UInt8;
using LineCapStyle = UInt8;

struct Point {
  x @0 :Int32;
  y @1 :Int32;
}

struct Size {
  width @0 :Int32;
  height @1 :Int32;
}

struct GuacServerInstruction {
  union {
    arc @0 :Arc;
    cfill @1 :Cfill;
    clip @2 :Layer;
    close @3 :Layer;
    copy @4 :Copy;
    cstroke @5 :Cstroke;
    cursor @6 :Cursor;
    curve @7 :Curve;
    dispose @8 :Layer;
    distort @9 :Distort;
    identity @10 :Layer;
    lfill @11 :Lfill;
    line @12 :Line;
    lstroke @13 :Lstroke;
    move @14 :Move;
    pop @15 :Layer;
    push @16 :Layer;
    rect @17 :Rect;
    reset @18 :Layer;
    set @19 :Set;
    shade @20 :Shade;
    size @21 :LayerSize;
    start @22 :Start;
    transfer @23 :Transfer;
    transform @24 :Transform;
    ack @25 :Ack;
    audio @26 :Audio;
    blob @27 :Blob;
    clipboard @28 :Clipboard;
    end @29 :Stream;
    file @30 :File;
    img @31 :Img;
    nest @32 :Nest;
    pipe @33 :Pipe;
    video @34 :Video;
    body @35 :Body;
    filesystem @36 :Filesystem;
    undefine @37 :Object;
    args @38 :List(Text);
    disconnect @39 :Void;
    error @40 :Error;
    log @41 :Text;
    mouse @42 :ServerMouse;
    key @43 :ServerKey;
    nop @44 :Void;
    ready @45 :Text;
    sync @46 :Int64;
    name @47 :Text;
  }
}

struct GuacClientInstruction {
  union {
    #get @0 :Get;
    #put @1 :Put;
    #audio @2 :Audio;
    #connect @3 :Connect;
    #image @4 :Image;
    #select @5 :Text;
    #key @6 :Key;
    #mouse @7 :Mouse;
    #initialSize @8 :InitialSize;
    #size @9 :Size;
    #video @10 :List(Text);
    #disconnect @11 :Void;
    #nop @12 :Void;
    #sync @13 :Int64;
    #clipboard @14 :Clipboard;
    #file @15 :File;
    #pipe @16 :Pipe;
    #ack @17 :Ack;
    #blob @18 :Blob;
    #end @19 :Stream;

    sync @0 :Int64;
    mouse @1 :ClientMouse;
    key @2 :ClientKey;
    clipboard @3 :Clipboard;
    disconnect @4 :Void;
    size @5 :Size;
    file @6 :File;
    pipe @7 :Pipe;
    ack @8 :Ack;
    blob @9 :Blob;
    end @10 :Stream;
    get @11 :Get;
    put @12 :Put;
    audio @13 :Audio;
    nop @14 :Void;
  }
}

struct Arc {
  layer @0 :Layer;
  x @1 :Int32;
  y @2 :Int32;
  radius @3 :Int32;
  start @4 :Float64;
  end @5 :Float64;
  negative @6 :Bool;
}

struct Cfill {
  mask @0 :CompositeMode;
  layer @1 :Layer;
  r @2 :Int32;
  g @3 :Int32;
  b @4 :Int32;
  a @5 :Int32;
}

struct Copy {
  srcLayer @0 :Layer;
  srcX @1 :Int32;
  srcY @2 :Int32;
  srcWidth @3 :Int32;
  srcHeight @4 :Int32;
  mask @5 :CompositeMode;
  dstLayer @6 :Layer;
  dstX @7 :Int32;
  dstY @8 :Int32;
}

struct Cstroke {
  mask @0 :CompositeMode;
  layer @1 :Layer;
  cap @2 :LineCapStyle;
  join @3 :LineJoinStyle;
  thickness @4 :Int32;
  r @5 :Int32;
  g @6 :Int32;
  b @7 :Int32;
  a @8 :Int32;
}

struct Cursor {
  x @0 :Int32;
  y @1 :Int32;
  srcLayer @2 :Layer;
  srcX @3 :Int32;
  srcY @4 :Int32;
  srcWidth @5 :Int32;
  srcHeight @6 :Int32;
}

struct Curve {
  layer @0 :Layer;
  cp1x @1 :Int32;
  cp1y @2 :Int32;
  cp2x @3 :Int32;
  cp2y @4 :Int32;
  x @5 :Int32;
  y @6 :Int32;
}

struct Distort {
  layer @0 :Layer;
  a @1 :Float64;
  b @2 :Float64;
  c @3 :Float64;
  d @4 :Float64;
  e @5 :Float64;
  f @6 :Float64;
}

struct Lfill {
  mask @0 :CompositeMode;
  layer @1 :Layer;
  srcLayer @2 :Layer;
}

struct Line {
  layer @0 :Layer;
  x @1 :Int32;
  y @2 :Int32;
}

struct Lstroke {
  mask @0 :CompositeMode;
  layer @1 :Layer;
  cap @2 :LineCap;
  join @3 :LineJoinStyle;
  thickness @4 :Int32;
  srcLayer @5 :Layer;

  enum LineCap {
    butt   @0;
    round  @1;
    square @2;
  }
}

struct Move {
  layer @0 :Layer;
  parent @1 :Layer;
  x @2 :Int32;
  y @3 :Int32;
  z @4 :Int32;
}

struct Rect {
  layer @0 :Layer;
  x @1 :Int32;
  y @2 :Int32;
  width @3 :Int32;
  height @4 :Int32;
}

struct Set {
  layer @0 :Layer;
  property @1 :Text;
  value @2 :Text;
}

struct Shade {
  layer @0 :Layer;
  opacity @1 :Int32;
}

struct LayerSize {
  layer @0 :Layer;
  width @1 :Int32;
  height @2 :Int32;
}

struct Start {
  layer @0 :Layer;
  x @1 :Int32;
  y @2 :Int32;
}

struct Transfer {
  srcLayer @0 :Layer;
  srcX @1 :Int32;
  srcY @2 :Int32;
  srcWidth @3 :Int32;
  srcHeight @4 :Int32;
  function @5 :UInt8;
  dstLayer @6 :Layer;
  dstX @7 :Int32;
  dstY @8 :Int32;
}

struct Transform {
  layer @0 :Layer;
  a @1 :Int64;
  b @2 :Int64;
  c @3 :Int64;
  d @4 :Int64;
  e @5 :Int64;
  f @6 :Int64;
}

struct Ack {
  stream @0 :Stream;
  message @1 :Text;
  status @2 :Status;
}

struct Audio {
  stream @0 :Stream;
  mimetype @1 :Text;
}

struct Blob {
  stream @0 :Stream;
  data @1 :Data;
}

struct Clipboard {
  stream @0 :Stream;
  mimetype @1 :Text;
}

struct File {
  stream @0 :Stream;
  mimetype @1 :Text;
  filename @2 :Text;
}

struct Img {
  stream @0 :Stream;
  mode @1 :UInt8;
  layer @2 :Layer;
  mimetype @3 :Text;
  x @4 :Int32;
  y @5 :Int32;
}

struct Nest {
  index @0 :Int32;
  data @1 :Data;
}

struct Pipe {
  stream @0 :Stream;
  mimetype @1 :Text;
  name @2 :Text;
}

struct Video {
  stream @0 :Stream;
  layer @1 :Layer;
  mimetype @2 :Text;
}

struct Body {
  object @0 :Object;
  stream @1 :Stream;
  mimetype @2 :Text;
  name @3 :Text;
}

struct Filesystem {
  object @0 :Object;
  name @1 :Text;
}

struct Get {
  object @0 :Object;
  name @1 :Text;
}

struct Put {
  object @0 :Object;
  stream @1 :Stream;
  mimetype @2 :Text;
  name @3 :Text;
}

struct InitialSize {
  width @0 :Int32;
  height @1 :Int32;
  dpi @2 :Int32;
}

struct Error {
  text @0 :Text;
  status @1 :Int32;
}

struct ClientKey {
  keysym @0 :Int32;
  pressed @1 :Bool;
}

struct ClientMouse {
  x @0 :Int32;
  y @1 :Int32;
  buttonMask @2 :Int32;
}

struct ServerMouse {
  x @0 :Int32;
  y @1 :Int32;
  buttonMask @2 :Int32;
  timestamp @3 :Int64;
}

struct ServerKey {
  keysym @0 :Int32;
  pressed @1 :Bool;
  timestamp @2 :Int64;
}

