/**
 * Copyright (c) 2006-2015 LOVE Development Team
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 **/

// LOVE
#include "common/config.h"
#include "common/math.h"
#include "common/Vector.h"

#include "Graphics.h"
#include "window/sdl/Window.h"
#include "Polyline.h"

// C++
#include <vector>
#include <sstream>
#include <algorithm>
#include <iterator>

// C
#include <cmath>
#include <cstdio>

namespace love
{
namespace graphics
{
namespace opengl
{

Graphics::Graphics()
	: width(0)
	, height(0)
	, created(false)
	, writingToStencil(false)
{
	gl = OpenGL();

	states.reserve(10);
	states.push_back(DisplayState());

	currentWindow = love::window::sdl::Window::createSingleton();

	int w, h;
	love::window::WindowSettings wsettings;

	currentWindow->getWindow(w, h, wsettings);

	if (currentWindow->isCreated())
		setMode(w, h, wsettings.sRGB);
}

Graphics::~Graphics()
{
	// We do this manually so the love objects get released before the window.
	states.clear();

	if (Shader::defaultShader)
	{
		Shader::defaultShader->release();
		Shader::defaultShader = nullptr;
	}

	currentWindow->release();
}

const char *Graphics::getName() const
{
	return "love.graphics.opengl";
}

void Graphics::restoreState(const DisplayState &s)
{
	setColor(s.color);
	setBackgroundColor(s.backgroundColor);

	setBlendMode(s.blendMode);

	setLineWidth(s.lineWidth);
	setLineStyle(s.lineStyle);
	setLineJoin(s.lineJoin);

	setPointSize(s.pointSize);

	if (s.scissor)
		setScissor(s.scissorBox.x, s.scissorBox.y, s.scissorBox.w, s.scissorBox.h);
	else
		setScissor();

	setStencilTest(s.stencilTest, s.stencilInvert);

	setFont(s.font.get());
	setShader(s.shader.get());
	setCanvas(s.canvases);

	setColorMask(s.colorMask);
	setWireframe(s.wireframe);

	setDefaultFilter(s.defaultFilter);
	setDefaultMipmapFilter(s.defaultMipmapFilter, s.defaultMipmapSharpness);
}

void Graphics::restoreStateChecked(const DisplayState &s)
{
	const DisplayState &cur = states.back();

	if (*(uint32 *) &s.color.r != *(uint32 *) &cur.color.r)
		setColor(s.color);

	if (*(uint32 *) &s.backgroundColor.r != *(uint32 *) &cur.backgroundColor.r)
		setBackgroundColor(s.backgroundColor);

	if (s.blendMode != cur.blendMode)
		setBlendMode(s.blendMode);

	// These are just simple assignments.
	setLineWidth(s.lineWidth);
	setLineStyle(s.lineStyle);
	setLineJoin(s.lineJoin);

	if (s.pointSize != cur.pointSize)
		setPointSize(s.pointSize);

	if (s.scissor != cur.scissor || (s.scissor && !(s.scissorBox == cur.scissorBox)))
	{
		if (s.scissor)
			setScissor(s.scissorBox.x, s.scissorBox.y, s.scissorBox.w, s.scissorBox.h);
		else
			setScissor();
	}

	if (s.stencilTest != cur.stencilTest || s.stencilInvert != cur.stencilInvert)
		setStencilTest(s.stencilTest, s.stencilInvert);

	setFont(s.font.get());
	setShader(s.shader.get());

	for (size_t i = 0; i < s.canvases.size() && i < cur.canvases.size(); i++)
	{
		if (s.canvases[i].get() != cur.canvases[i].get())
		{
			setCanvas(s.canvases);
			break;
		}
	}

	for (int i = 0; i < 4; i++)
	{
		if (s.colorMask[i] != cur.colorMask[i])
		{
			setColorMask(s.colorMask);
			break;
		}
	}

	if (s.wireframe != cur.wireframe)
		setWireframe(s.wireframe);

	setDefaultFilter(s.defaultFilter);
	setDefaultMipmapFilter(s.defaultMipmapFilter, s.defaultMipmapSharpness);
}

void Graphics::setViewportSize(int width, int height)
{
	this->width = width;
	this->height = height;

	if (!isCreated())
		return;

	// We want to affect the main screen, not any Canvas that's currently active
	// (not that any *should* be active when this is called.)
	std::vector<Object::StrongRef<Canvas>> canvases = states.back().canvases;
	setCanvas();

	// Set the viewport to top-left corner.
	gl.setViewport(OpenGL::Viewport(0, 0, width, height));

	// If a canvas was bound before this function was called, it needs to be
	// made aware of the new system viewport size.
	Canvas::systemViewport = gl.getViewport();

	// Set up the projection matrix
	gl.matrices.projection.back() = Matrix::ortho(0.0, width, height, 0.0);

	// Restore the previously active Canvas.
	setCanvas(canvases);
}

bool Graphics::setMode(int width, int height, bool &sRGB)
{
	this->width = width;
	this->height = height;

	// Okay, setup OpenGL.
	gl.initContext();
	gl.setupContext();

	created = true;

	setViewportSize(width, height);

	// Make sure antialiasing works when set elsewhere
	glEnable(GL_MULTISAMPLE);

	// Enable blending
	glEnable(GL_BLEND);

	// Auto-generated mipmaps should be the best quality possible
	glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);

	// Enable textures
	glEnable(GL_TEXTURE_2D);
	gl.setTextureUnit(0);

	// Set pixel row alignment
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	// Set whether drawing converts input from linear -> sRGB colorspace.
	if (GLAD_VERSION_3_0 || GLAD_ARB_framebuffer_sRGB || GLAD_EXT_framebuffer_sRGB
		|| GLAD_ES_VERSION_3_0 || GLAD_EXT_sRGB)
	{
		if (GLAD_VERSION_1_0 || GLAD_EXT_sRGB_write_control)
		{
			if (sRGB)
				glEnable(GL_FRAMEBUFFER_SRGB);
			else
				glDisable(GL_FRAMEBUFFER_SRGB);
		}
	}
	else
		sRGB = false;

	Canvas::screenHasSRGB = sRGB;

	bool enabledebug = false;

	if (GLAD_VERSION_3_0)
	{
		// Enable OpenGL's debug output if a debug context has been created.
		GLint flags = 0;
		glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
		enabledebug = (flags & GL_CONTEXT_FLAG_DEBUG_BIT) != 0;
	}

	setDebug(enabledebug);

	// Reload all volatile objects.
	if (!Volatile::loadAll())
		::printf("Could not reload all volatile objects.\n");

	// Restore the graphics state.
	restoreState(states.back());

	pixel_size_stack.clear();
	pixel_size_stack.reserve(5);
	pixel_size_stack.push_back(1);

	// We always need a default shader.
	if (!Shader::defaultShader)
	{
		Renderer renderer = GLAD_ES_VERSION_2_0 ? RENDERER_OPENGLES : RENDERER_OPENGL;
		Shader::defaultShader = newShader(Shader::defaultCode[renderer]);
	}

	if (!getShader())
		setShader(Shader::defaultShader);

	return true;
}

void Graphics::unSetMode()
{
	if (!isCreated())
		return;

	// Unload all volatile objects. These must be reloaded after the display
	// mode change.
	Volatile::unloadAll();

	gl.deInitContext();

	created = false;
}

static void APIENTRY debugCB(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei /*len*/, const GLchar *msg, const GLvoid* /*usr*/)
{
	// Human-readable strings for the debug info.
	const char *sourceStr = OpenGL::debugSourceString(source);
	const char *typeStr = OpenGL::debugTypeString(type);
	const char *severityStr = OpenGL::debugSeverityString(severity);

	const char *fmt = "OpenGL: %s [source=%s, type=%s, severity=%s, id=%d]\n";
	printf(fmt, msg, sourceStr, typeStr, severityStr, id);
}

void Graphics::setDebug(bool enable)
{
	// Make sure debug output is supported. The AMD ext. is a bit different
	// so we don't make use of it, since AMD drivers now support KHR_debug.
	if (!(GLAD_VERSION_4_3 || GLAD_KHR_debug || GLAD_ARB_debug_output))
		return;

	// TODO: We don't support GL_KHR_debug in GLES yet.
	if (GLAD_ES_VERSION_2_0)
		return;

	// Ugly hack to reduce code duplication.
	if (GLAD_ARB_debug_output && !(GLAD_VERSION_4_3 || GLAD_KHR_debug))
	{
		fp_glDebugMessageCallback = (pfn_glDebugMessageCallback) fp_glDebugMessageCallbackARB;
		fp_glDebugMessageControl = (pfn_glDebugMessageControl) fp_glDebugMessageControlARB;
	}

	if (!enable)
	{
		// Disable the debug callback function.
		glDebugMessageCallback(nullptr, nullptr);

		// We can disable debug output entirely with KHR_debug.
		if (GLAD_VERSION_4_3 || GLAD_KHR_debug)
			glDisable(GL_DEBUG_OUTPUT);

		return;
	}

	// We don't want asynchronous debug output.
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);

	glDebugMessageCallback(debugCB, nullptr);

	// Initially, enable everything.
	glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, 0, GL_TRUE);

	// Disable messages about deprecated OpenGL functionality.
	glDebugMessageControl(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR, GL_DONT_CARE, 0, 0, GL_FALSE);
	glDebugMessageControl(GL_DEBUG_SOURCE_SHADER_COMPILER, GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR, GL_DONT_CARE, 0, 0, GL_FALSE);

	if (GLAD_VERSION_4_3 || GLAD_KHR_debug)
		glEnable(GL_DEBUG_OUTPUT);

	::printf("OpenGL debug output enabled (LOVE_GRAPHICS_DEBUG=1)\n");
}

void Graphics::reset()
{
	DisplayState s;
	drawToStencilBuffer(false);
	restoreState(s);
	origin();
}

void Graphics::clear(ClearType type)
{
	GLbitfield mask = 0;

	switch (type)
	{
	case CLEAR_ALL:
	default:
		mask = GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT | GL_DEPTH_BUFFER_BIT;
		break;
	case CLEAR_STENCIL:
		mask = GL_STENCIL_BUFFER_BIT;
		break;
	}

	glClear(mask);
}

void Graphics::present()
{
	// Make sure we don't have a canvas active.
	std::vector<Object::StrongRef<Canvas>> canvases = states.back().canvases;
	setCanvas();

	if (GLAD_ES_VERSION_3_0 || GLAD_EXT_discard_framebuffer)
	{
		GLenum attachments[] = {GL_STENCIL, GL_DEPTH};

		if (gl.getDefaultFBO() != 0)
		{
			// A non-zero FBO needs different attachment enums.
			attachments[0] = GL_STENCIL_ATTACHMENT;
			attachments[1] = GL_DEPTH_ATTACHMENT;
		}

		// Hint for the driver that it doesn't need to save these buffers.
		if (GLAD_ES_VERSION_3_0)
			glInvalidateFramebuffer(GL_FRAMEBUFFER, 2, attachments);
		else if (GLAD_EXT_discard_framebuffer)
			glDiscardFramebufferEXT(GL_FRAMEBUFFER, 2, attachments);
	}

	currentWindow->swapBuffers();

	// Restore the currently active canvas, if there is one.
	setCanvas(canvases);

	// Reset the per-frame stat counts.
	gl.stats.drawCalls = 0;
	Canvas::switchCount = 0;
}

int Graphics::getWidth() const
{
	return width;
}

int Graphics::getHeight() const
{
	return height;
}

bool Graphics::isCreated() const
{
	return created;
}

void Graphics::setScissor(int x, int y, int width, int height)
{
	OpenGL::Viewport box(x, y, width, height);

	states.back().scissor = true;
	glEnable(GL_SCISSOR_TEST);
	// OpenGL's reversed y-coordinate is compensated for in OpenGL::setScissor.
	gl.setScissor(box);
	states.back().scissorBox = box;
}

void Graphics::setScissor()
{
	states.back().scissor = false;
	glDisable(GL_SCISSOR_TEST);
}

bool Graphics::getScissor(int &x, int &y, int &width, int &height) const
{
	OpenGL::Viewport scissor = gl.getScissor();

	x = scissor.x;
	y = scissor.y;
	width = scissor.w;
	height = scissor.h;

	return states.back().scissor;
}

void Graphics::drawToStencilBuffer(bool enable)
{
	if (writingToStencil == enable)
		return;

	writingToStencil = enable;

	if (!enable)
	{
		const DisplayState &state = states.back();

		// Revert the color write mask.
		setColorMask(state.colorMask);

		// Use the user-set stencil test state when writes are disabled.
		setStencilTest(state.stencilTest, state.stencilInvert);
		return;
	}

	// Make sure the active canvas has a stencil buffer.
	if (Canvas::current)
		Canvas::current->checkCreateStencil();

	// Disable color writes but don't save the state for it.
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

	// The stencil test must be enabled in order to write to the stencil buffer.
	glEnable(GL_STENCIL_TEST);

	glStencilFunc(GL_ALWAYS, 1, 1);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
}

void Graphics::setStencilTest(bool enable, bool invert)
{
	DisplayState &state = states.back();
	state.stencilTest = enable;
	state.stencilInvert = invert;

	if (writingToStencil)
		return;

	if (!enable)
	{
		glDisable(GL_STENCIL_TEST);
		return;
	}

	// Make sure the active canvas has a stencil buffer.
	if (Canvas::current)
		Canvas::current->checkCreateStencil();

	glEnable(GL_STENCIL_TEST);
	glStencilFunc(GL_EQUAL, invert ? 0 : 1, 1);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
}

void Graphics::getStencilTest(bool &enable, bool &invert)
{
	const DisplayState &state = states.back();
	enable = state.stencilTest;
	invert = state.stencilInvert;
}

Image *Graphics::newImage(love::image::ImageData *data, const Image::Flags &flags)
{
	// Create the image.
	Image *image = new Image(data, flags);

	if (!isCreated())
		return image;

	bool success = false;
	try
	{
		success = image->load();
	}
	catch(love::Exception &)
	{
		image->release();
		throw;
	}
	if (!success)
	{
		image->release();
		return nullptr;
	}

	return image;
}

Image *Graphics::newImage(love::image::CompressedData *cdata, const Image::Flags &flags)
{
	// Create the image.
	Image *image = new Image(cdata, flags);

	if (!isCreated())
		return image;

	bool success = false;
	try
	{
		success = image->load();
	}
	catch(love::Exception &)
	{
		image->release();
		throw;
	}
	if (!success)
	{
		image->release();
		return nullptr;
	}

	return image;
}

Quad *Graphics::newQuad(Quad::Viewport v, float sw, float sh)
{
	return new Quad(v, sw, sh);
}

Font *Graphics::newFont(love::font::Rasterizer *r, const Texture::Filter &filter)
{
	return new Font(r, filter);
}

SpriteBatch *Graphics::newSpriteBatch(Texture *texture, int size, int usage)
{
	return new SpriteBatch(texture, size, usage);
}

ParticleSystem *Graphics::newParticleSystem(Texture *texture, int size)
{
	return new ParticleSystem(texture, size);
}

Canvas *Graphics::newCanvas(int width, int height, Canvas::Format format, int msaa)
{
	if (!Canvas::isFormatSupported(format))
	{
		const char *fstr = "rgba8";
		Canvas::getConstant(format, fstr);
		throw love::Exception("The %s canvas format is not supported by your OpenGL implementation.", fstr);
	}

	if (width > gl.getMaxTextureSize())
		throw Exception("Cannot create canvas: width of %d pixels is too large for this system.", width);
	else if (height > gl.getMaxTextureSize())
		throw Exception("Cannot create canvas: height of %d pixels is too large for this system.", height);

	while (GL_NO_ERROR != glGetError())
		/* clear opengl error flag */;

	Canvas *canvas = new Canvas(width, height, format, msaa);
	GLenum err = canvas->getStatus();

	// everything ok, return canvas (early out)
	if (err == GL_FRAMEBUFFER_COMPLETE)
		return canvas;

	// create error message
	std::stringstream error_string;
	error_string << "Cannot create canvas: ";
	switch (err)
	{

	case GL_FRAMEBUFFER_UNSUPPORTED:
		error_string << "Not supported by your OpenGL implementation.";
		break;

		// remaining error codes are highly unlikely:
	case GL_FRAMEBUFFER_UNDEFINED:
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
	case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
	case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
	case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
	case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
		error_string << "Error in implementation. Possible fix: Make canvas width and height powers of two.";
		break;

	default:
		// my intel hda card wrongly returns 0 to glCheckFramebufferStatus() but sets
		// no error flag. I think it meant to return GL_FRAMEBUFFER_UNSUPPORTED, but who
		// knows.
		if (glGetError() == GL_NO_ERROR)
			error_string << "May not be supported by your OpenGL implementation.";
		// the remaining error is an indication of a serious fuckup since it should
		// only be returned if glCheckFramebufferStatus() was called with the wrong
		// arguments.
		else
			error_string << "Cannot create canvas: Aliens did it (OpenGL error code: " << glGetError() << ")";
	}

	canvas->release();
	throw Exception("%s", error_string.str().c_str());
	return nullptr; // never reached
}

Shader *Graphics::newShader(const Shader::ShaderSource &source)
{
	return new Shader(source);
}

Mesh *Graphics::newMesh(const std::vector<Vertex> &vertices, Mesh::DrawMode mode)
{
	return new Mesh(vertices, mode);
}

Mesh *Graphics::newMesh(int vertexcount, Mesh::DrawMode mode)
{
	return new Mesh(vertexcount, mode);
}

Text *Graphics::newText(Font *font, const std::string &text)
{
	return new Text(font, text);
}

void Graphics::setColor(const Color &c)
{
	gl.setColor(c);
	states.back().color = c;
}

Color Graphics::getColor() const
{
	return states.back().color;
}

void Graphics::setBackgroundColor(const Color &c)
{
	gl.setClearColor(c);
	states.back().backgroundColor = c;
}

Color Graphics::getBackgroundColor() const
{
	return states.back().backgroundColor;
}

void Graphics::setFont(Font *font)
{
	// Hack: the Lua-facing love.graphics.print function will set the current
	// font if needed, but only on its first call... we want to make sure a nil
	// font is never accidentally set (e.g. via love.graphics.reset.)
	if (font == nullptr)
		return;

	DisplayState &state = states.back();
	state.font.set(font);
}

Font *Graphics::getFont() const
{
	return states.back().font.get();
}

void Graphics::setShader(Shader *shader)
{
	if (shader == nullptr)
		return setShader();

	DisplayState &state = states.back();

	shader->attach();

	state.shader.set(shader);
}

void Graphics::setShader()
{
	DisplayState &state = states.back();

	Shader::detach();

	state.shader.set(nullptr);
}

Shader *Graphics::getShader() const
{
	return states.back().shader.get();
}

void Graphics::setCanvas(Canvas *canvas)
{
	if (canvas == nullptr)
		return setCanvas();

	DisplayState &state = states.back();

	canvas->startGrab();

	std::vector<Object::StrongRef<Canvas>> canvasref;
	canvasref.push_back(canvas);

	std::swap(state.canvases, canvasref);
}

void Graphics::setCanvas(const std::vector<Canvas *> &canvases)
{
	if (canvases.size() == 0)
		return setCanvas();
	else if (canvases.size() == 1)
		return setCanvas(canvases[0]);

	DisplayState &state = states.back();

	auto attachments = std::vector<Canvas *>(canvases.begin() + 1, canvases.end());
	canvases[0]->startGrab(attachments);

	std::vector<Object::StrongRef<Canvas>> canvasrefs;
	canvasrefs.reserve(canvases.size());

	for (Canvas *c : canvases)
		canvasrefs.push_back(c);

	std::swap(state.canvases, canvasrefs);
}

void Graphics::setCanvas(const std::vector<Object::StrongRef<Canvas>> &canvases)
{
	std::vector<Canvas *> canvaslist;
	canvaslist.reserve(canvases.size());

	for (const Object::StrongRef<Canvas> &c : canvases)
		canvaslist.push_back(c.get());

	return setCanvas(canvaslist);
}

void Graphics::setCanvas()
{
	DisplayState &state = states.back();

	if (Canvas::current != nullptr)
		Canvas::current->stopGrab();

	state.canvases.clear();
}

std::vector<Canvas *> Graphics::getCanvas() const
{
	std::vector<Canvas *> canvases;
	canvases.reserve(states.back().canvases.size());

	for (const Object::StrongRef<Canvas> &c : states.back().canvases)
		canvases.push_back(c.get());

	return canvases;
}

void Graphics::setColorMask(const bool mask[4])
{
	for (int i = 0; i < 4; i++)
		states.back().colorMask[i] = mask[i];

	glColorMask(mask[0], mask[1], mask[2], mask[3]);
}

const bool *Graphics::getColorMask() const
{
	return states.back().colorMask;
}

void Graphics::setBlendMode(Graphics::BlendMode mode)
{
	OpenGL::BlendState blend = {GL_ONE, GL_ONE, GL_ZERO, GL_ZERO, GL_FUNC_ADD};

	switch (mode)
	{
	case BLEND_ALPHA:
		blend.srcRGB = GL_SRC_ALPHA;
		blend.srcA = GL_ONE;
		blend.dstRGB = blend.dstA = GL_ONE_MINUS_SRC_ALPHA;
		break;
	case BLEND_MULTIPLY:
		blend.srcRGB = blend.srcA = GL_DST_COLOR;
		blend.dstRGB = blend.dstA = GL_ZERO;
		break;
	case BLEND_PREMULTIPLIED:
		blend.srcRGB = blend.srcA = GL_ONE;
		blend.dstRGB = blend.dstA = GL_ONE_MINUS_SRC_ALPHA;
		break;
	case BLEND_SUBTRACT:
		blend.func = GL_FUNC_REVERSE_SUBTRACT;
	case BLEND_ADD:
		blend.srcRGB = blend.srcA = GL_SRC_ALPHA;
		blend.dstRGB = blend.dstA = GL_ONE;
		break;
	case BLEND_SCREEN:
		blend.srcRGB = blend.srcA = GL_ONE;
		blend.dstRGB = blend.dstA = GL_ONE_MINUS_SRC_COLOR;
		break;
	case BLEND_REPLACE:
	default:
		blend.srcRGB = blend.srcA = GL_ONE;
		blend.dstRGB = blend.dstA = GL_ZERO;
		break;
	}

	gl.setBlendState(blend);
	states.back().blendMode = mode;
}

Graphics::BlendMode Graphics::getBlendMode() const
{
	return states.back().blendMode;
}

void Graphics::setDefaultFilter(const Texture::Filter &f)
{
	Texture::setDefaultFilter(f);
	states.back().defaultFilter = f;
}

const Texture::Filter &Graphics::getDefaultFilter() const
{
	return Texture::getDefaultFilter();
}

void Graphics::setDefaultMipmapFilter(Texture::FilterMode filter, float sharpness)
{
	Image::setDefaultMipmapFilter(filter);
	Image::setDefaultMipmapSharpness(sharpness);

	states.back().defaultMipmapFilter = filter;
	states.back().defaultMipmapSharpness = sharpness;
}

void Graphics::getDefaultMipmapFilter(Texture::FilterMode *filter, float *sharpness) const
{
	*filter = Image::getDefaultMipmapFilter();
	*sharpness = Image::getDefaultMipmapSharpness();
}

void Graphics::setLineWidth(float width)
{
	states.back().lineWidth = width;
}

void Graphics::setLineStyle(Graphics::LineStyle style)
{
	states.back().lineStyle = style;
}

void Graphics::setLineJoin(Graphics::LineJoin join)
{
	states.back().lineJoin = join;
}

float Graphics::getLineWidth() const
{
	return states.back().lineWidth;
}

Graphics::LineStyle Graphics::getLineStyle() const
{
	return states.back().lineStyle;
}

Graphics::LineJoin Graphics::getLineJoin() const
{
	return states.back().lineJoin;
}

void Graphics::setPointSize(float size)
{
	gl.setPointSize(size);
	states.back().pointSize = size;
}

float Graphics::getPointSize() const
{
	return states.back().pointSize;
}

void Graphics::setWireframe(bool enable)
{
	// Not supported in OpenGL ES.
	if (GLAD_ES_VERSION_2_0)
		return;

	glPolygonMode(GL_FRONT_AND_BACK, enable ? GL_LINE : GL_FILL);
	states.back().wireframe = enable;
}

bool Graphics::isWireframe() const
{
	return states.back().wireframe;
}

void Graphics::print(const std::string &str, float x, float y , float angle, float sx, float sy, float ox, float oy, float kx, float ky)
{
	DisplayState &state = states.back();

	if (state.font.get() != nullptr)
		state.font->print(str, x, y, angle, sx, sy, ox, oy, kx, ky);
}

void Graphics::printf(const std::string &str, float x, float y, float wrap, Font::AlignMode align, float angle, float sx, float sy, float ox, float oy, float kx, float ky)
{
	DisplayState &state = states.back();

	if (state.font.get() != nullptr)
		state.font->printf(str, x, y, wrap, align, angle, sx, sy, ox, oy, kx, ky);
}

/**
 * Primitives
 **/

void Graphics::point(float x, float y)
{
	GLfloat coord[] = {x, y};

	gl.prepareDraw();
	gl.bindTexture(gl.getDefaultTexture());
	glEnableVertexAttribArray(ATTRIB_POS);
	glVertexAttribPointer(ATTRIB_POS, 2, GL_FLOAT, GL_FALSE, 0, coord);
	gl.drawArrays(GL_POINTS, 0, 1);
	glDisableVertexAttribArray(ATTRIB_POS);
}

void Graphics::polyline(const float *coords, size_t count)
{
	DisplayState &state = states.back();

	if (state.lineJoin == LINE_JOIN_NONE)
	{
			NoneJoinPolyline line;
			line.render(coords, count, state.lineWidth * .5f, float(pixel_size_stack.back()), state.lineStyle == LINE_SMOOTH);
			line.draw();
	}
	else if (state.lineJoin == LINE_JOIN_BEVEL)
	{
		BevelJoinPolyline line;
		line.render(coords, count, state.lineWidth * .5f, float(pixel_size_stack.back()), state.lineStyle == LINE_SMOOTH);
		line.draw();
	}
	else // LINE_JOIN_MITER
	{
		MiterJoinPolyline line;
		line.render(coords, count, state.lineWidth * .5f, float(pixel_size_stack.back()), state.lineStyle == LINE_SMOOTH);
		line.draw();
	}
}

void Graphics::rectangle(DrawMode mode, float x, float y, float w, float h)
{
	float coords[] = {x,y, x,y+h, x+w,y+h, x+w,y, x,y};
	polygon(mode, coords, 5 * 2);
}

void Graphics::circle(DrawMode mode, float x, float y, float radius, int points)
{
	float two_pi = static_cast<float>(LOVE_M_PI * 2);
	if (points <= 0) points = 1;
	float angle_shift = (two_pi / points);
	float phi = .0f;

	float *coords = new float[2 * (points + 1)];
	for (int i = 0; i < points; ++i, phi += angle_shift)
	{
		coords[2*i]   = x + radius * cosf(phi);
		coords[2*i+1] = y + radius * sinf(phi);
	}

	coords[2*points]   = coords[0];
	coords[2*points+1] = coords[1];

	polygon(mode, coords, (points + 1) * 2);

	delete[] coords;
}

void Graphics::arc(DrawMode mode, float x, float y, float radius, float angle1, float angle2, int points)
{
	// Nothing to display with no points or equal angles. (Or is there with line mode?)
	if (points <= 0 || angle1 == angle2)
		return;

	// Oh, you want to draw a circle?
	if (fabs(angle1 - angle2) >= 2.0f * (float) LOVE_M_PI)
	{
		circle(mode, x, y, radius, points);
		return;
	}

	float angle_shift = (angle2 - angle1) / points;
	// Bail on precision issues.
	if (angle_shift == 0.0)
		return;

	float phi = angle1;
	int num_coords = (points + 3) * 2;
	float *coords = new float[num_coords];
	coords[0] = coords[num_coords - 2] = x;
	coords[1] = coords[num_coords - 1] = y;

	for (int i = 0; i <= points; ++i, phi += angle_shift)
	{
		coords[2 * (i+1)]     = x + radius * cosf(phi);
		coords[2 * (i+1) + 1] = y + radius * sinf(phi);
	}

	// GL_POLYGON can only fill-draw convex polygons, so we need to do stuff manually here
	if (mode == DRAW_LINE)
	{
		polyline(coords, num_coords); // Artifacts at sharp angles if set to looping.
	}
	else
	{
		gl.prepareDraw();
		gl.bindTexture(gl.getDefaultTexture());
		glEnableVertexAttribArray(ATTRIB_POS);
		glVertexAttribPointer(ATTRIB_POS, 2, GL_FLOAT, GL_FALSE, 0, coords);
		gl.drawArrays(GL_TRIANGLE_FAN, 0, points + 2);
		glDisableVertexAttribArray(ATTRIB_POS);
	}

	delete[] coords;
}

/// @param mode    the draw mode
/// @param coords  the coordinate array
/// @param count   the number of coordinates/size of the array
void Graphics::polygon(DrawMode mode, const float *coords, size_t count)
{
	// coords is an array of a closed loop of vertices, i.e.
	// coords[count-2] = coords[0], coords[count-1] = coords[1]
	if (mode == DRAW_LINE)
	{
		polyline(coords, count);
	}
	else
	{
		gl.prepareDraw();
		gl.bindTexture(gl.getDefaultTexture());
		glEnableVertexAttribArray(ATTRIB_POS);
		glVertexAttribPointer(ATTRIB_POS, 2, GL_FLOAT, GL_FALSE, 0, coords);
		gl.drawArrays(GL_TRIANGLE_FAN, 0, count/2-1); // opengl will close the polygon for us
		glDisableVertexAttribArray(ATTRIB_POS);
	}
}

love::image::ImageData *Graphics::newScreenshot(love::image::Image *image, bool copyAlpha)
{
	// Temporarily unbind the currently active canvas (glReadPixels reads the
	// active framebuffer, not the main one.)
	std::vector<Object::StrongRef<Canvas>> canvases = states.back().canvases;
	setCanvas();

	int w = getWidth();
	int h = getHeight();

	int row = 4 * w;
	int size = row * h;

	GLubyte *pixels = nullptr;
	GLubyte *screenshot = nullptr;

	try
	{
		pixels = new GLubyte[size];
		screenshot = new GLubyte[size];
	}
	catch (std::exception &)
	{
		delete[] pixels;
		delete[] screenshot;
		setCanvas(canvases);
		throw love::Exception("Out of memory.");
	}

	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

	if (!copyAlpha)
	{
		// Replace alpha values with full opacity.
		for (int i = 3; i < size; i += 4)
			pixels[i] = 255;
	}

	// OpenGL sucks and reads pixels from the lower-left. Let's fix that.
	GLubyte *src = pixels - row;
	GLubyte *dst = screenshot + size;

	for (int i = 0; i < h; ++i)
		memcpy(dst-=row, src+=row, row);

	delete[] pixels;

	love::image::ImageData *img = nullptr;
	try
	{
		// Tell the new ImageData that it owns the screenshot data, so we don't
		// need to delete it here.
		img = image->newImageData(w, h, (void *) screenshot, true);
	}
	catch (love::Exception &)
	{
		delete[] screenshot;
		setCanvas(canvases);
		throw;
	}

	// Re-bind the active canvas, if necessary.
	setCanvas(canvases);

	return img;
}

Graphics::RendererInfo Graphics::getRendererInfo() const
{
	RendererInfo info;

	if (GLAD_ES_VERSION_2_0)
		info.name = "OpenGL ES";
	else
		info.name = "OpenGL";

	const char *str = (const char *) glGetString(GL_VERSION);
	if (str)
		info.version = str;
	else
		throw love::Exception("Cannot retrieve renderer version information.");

	str = (const char *) glGetString(GL_VENDOR);
	if (str)
		info.vendor = str;
	else
		throw love::Exception("Cannot retrieve renderer vendor information.");

	str = (const char *) glGetString(GL_RENDERER);
	if (str)
		info.device = str;
	else
		throw love::Exception("Cannot retrieve renderer device information.");

	return info;
}

Graphics::Stats Graphics::getStats() const
{
	Stats stats;

	stats.drawCalls = gl.stats.drawCalls;
	stats.canvasSwitches = Canvas::switchCount;
	stats.canvases = Canvas::canvasCount;
	stats.images = Image::imageCount;
	stats.fonts = Font::fontCount;
	stats.textureMemory = gl.stats.textureMemory;

	return stats;
}

double Graphics::getSystemLimit(SystemLimit limittype) const
{
	double limit = 0.0;

	switch (limittype)
	{
	case Graphics::LIMIT_POINT_SIZE:
		{
			GLfloat limits[2];
			glGetFloatv(GL_ALIASED_POINT_SIZE_RANGE, limits);
			limit = limits[1];
		}
		break;
	case Graphics::LIMIT_TEXTURE_SIZE:
		limit = (double) gl.getMaxTextureSize();
		break;
	case Graphics::LIMIT_MULTI_CANVAS:
		limit = (double) gl.getMaxRenderTargets();
		break;
	case Graphics::LIMIT_CANVAS_MSAA:
		if (GLAD_VERSION_3_0 || GLAD_ARB_framebuffer_object
			|| GLAD_EXT_framebuffer_multisample)
		{
			GLint intlimit = 0;
			glGetIntegerv(GL_MAX_SAMPLES, &intlimit);
			limit = (double) intlimit;
		}
		break;
	default:
		break;
	}

	return limit;
}

bool Graphics::isSupported(Support feature) const
{
	switch (feature)
	{
	case SUPPORT_MULTI_CANVAS:
		return Canvas::isMultiCanvasSupported();
	case SUPPORT_SRGB:
		// sRGB support for the screen is guaranteed if it's supported as a
		// Canvas format.
		return Canvas::isFormatSupported(Canvas::FORMAT_SRGB);
	default:
		return false;
	}
}

void Graphics::push(StackType type)
{
	if (stackTypes.size() == MAX_USER_STACK_DEPTH)
		throw Exception("Maximum stack depth reached (more pushes than pops?)");

	gl.pushTransform();

	pixel_size_stack.push_back(pixel_size_stack.back());

	if (type == STACK_ALL)
		states.push_back(states.back());

	stackTypes.push_back(type);
}

void Graphics::pop()
{
	if (stackTypes.size() < 1)
		throw Exception("Minimum stack depth reached (more pops than pushes?)");

	gl.popTransform();
	pixel_size_stack.pop_back();

	if (stackTypes.back() == STACK_ALL)
	{
		DisplayState &newstate = states[states.size() - 2];

		restoreStateChecked(newstate);

		// The last two states in the stack should be equal now.
		states.pop_back();
	}

	stackTypes.pop_back();
}

void Graphics::rotate(float r)
{
	gl.getTransform().rotate(r);
}

void Graphics::scale(float x, float y)
{
	gl.getTransform().scale(x, y);
	pixel_size_stack.back() *= 2. / (fabs(x) + fabs(y));
}

void Graphics::translate(float x, float y)
{
	gl.getTransform().translate(x, y);
}

void Graphics::shear(float kx, float ky)
{
	gl.getTransform().setShear(kx, ky);
}

void Graphics::origin()
{
	gl.getTransform().setIdentity();
	pixel_size_stack.back() = 1;
}

Graphics::DisplayState::DisplayState()
	: color(255, 255, 255, 255)
	, backgroundColor(0, 0, 0, 255)
	, blendMode(BLEND_ALPHA)
	, lineWidth(1.0f)
	, lineStyle(LINE_SMOOTH)
	, lineJoin(LINE_JOIN_MITER)
	, pointSize(1.0f)
	, scissor(false)
	, scissorBox()
	, stencilTest(false)
	, stencilInvert(false)
	, font(nullptr)
	, shader(nullptr)
	, wireframe(false)
	, defaultFilter()
	, defaultMipmapFilter(Texture::FILTER_NEAREST)
	, defaultMipmapSharpness(0.0f)
{
	// We should just directly initialize the array in the initializer list, but
	// that feature of C++11 is broken in Visual Studio 2013...
	colorMask[0] = colorMask[1] = colorMask[2] = colorMask[3] = true;
}

Graphics::DisplayState::DisplayState(const DisplayState &other)
	: color(other.color)
	, backgroundColor(other.backgroundColor)
	, blendMode(other.blendMode)
	, lineWidth(other.lineWidth)
	, lineStyle(other.lineStyle)
	, lineJoin(other.lineJoin)
	, pointSize(other.pointSize)
	, scissor(other.scissor)
	, scissorBox(other.scissorBox)
	, stencilTest(other.stencilTest)
	, stencilInvert(other.stencilInvert)
	, font(other.font)
	, shader(other.shader)
	, canvases(other.canvases)
	, wireframe(other.wireframe)
	, defaultFilter(other.defaultFilter)
	, defaultMipmapFilter(other.defaultMipmapFilter)
	, defaultMipmapSharpness(other.defaultMipmapSharpness)
{
	for (int i = 0; i < 4; i++)
		colorMask[i] = other.colorMask[i];
}

Graphics::DisplayState::~DisplayState()
{
}

Graphics::DisplayState &Graphics::DisplayState::operator = (const DisplayState &other)
{
	color = other.color;
	backgroundColor = other.backgroundColor;
	blendMode = other.blendMode;
	lineWidth = other.lineWidth;
	lineStyle = other.lineStyle;
	lineJoin = other.lineJoin;
	pointSize = other.pointSize;
	scissor = other.scissor;
	scissorBox = other.scissorBox;
	stencilTest = other.stencilTest;
	stencilInvert = other.stencilInvert;

	font = other.font;
	shader = other.shader;
	canvases = other.canvases;

	for (int i = 0; i < 4; i++)
		colorMask[i] = other.colorMask[i];

	wireframe = other.wireframe;

	defaultFilter = other.defaultFilter;
	defaultMipmapFilter = other.defaultMipmapFilter;
	defaultMipmapSharpness = other.defaultMipmapSharpness;

	return *this;
}

} // opengl
} // graphics
} // love
