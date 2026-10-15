#pragma once
#include "ogplay/app/json.h"
#include "ogplay/core/json.h"
namespace ogplay::app { struct JsonAccess { static JsonValue Wrap(core::JsonValue value); }; }
