#pragma once

#include <astream/Config.hpp>
#include <astream/ConnectionContext.hpp>
#include <astream/Debug.hpp>
#include <astream/Engine.hpp>
#include <astream/GameWorld.hpp>
#include <astream/InputDevice.hpp>
#include <astream/auth/RegisterPolicy.hpp>
#include <astream/graphics/GraphicsDevice.hpp>
#include <astream/graphics/MeshView.hpp>
#include <astream/graphics/Texture2D.hpp>
#include <astream/graphics/Transform.hpp>
#include <astream/graphics/shaders/DefaultShaders.hpp>

namespace astream {

using astream::EngineConfig;
// Expose graphics components to astream namespace
using graphics::Texture2D;
using graphics::TextureView;

using graphics::convertible_to_texture_view;

using graphics::GraphicsDevice;
using graphics::IsComputeShader;
using graphics::IsFragmentShader;
using graphics::IsVarying;
using graphics::IsVertexShader;

using graphics::MeshView;
using graphics::Transform;

using auth::RegisterPolicy;

namespace Shaders {
using graphics::shaders::DefaultVarying;
using graphics::shaders::DefaultVertex;
using graphics::shaders::deferredLightingCS;
using graphics::shaders::geometryVS;
using graphics::shaders::mapIntensityToChar;
using graphics::shaders::shadowVS;
} // namespace Shaders

} // namespace astream
