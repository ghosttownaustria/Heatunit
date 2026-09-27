#pragma once
// miniaudio is a single-header library that is compiled once (MiniaudioImpl.cpp). Whatever changes the layout
// of its types has to be set identically wherever the header is included, hence this one place. Only device
// output is needed: no decoding, encoding, sound generation, sound engine or node graph.
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
