// Copyright (c)2007 Nicholas Piegdon
// See license.txt for license information

#include "Renderer.h"
#include "Tga.h"
#include "os_graphics.h"

#include <limits>

// Static variable for OpenGL texture ID
static unsigned int last_texture_id = std::numeric_limits<unsigned int>::max();

// Function to bind texture if it has changed
void SelectTexture(unsigned int texture_id)
{
    if (texture_id != last_texture_id) {
        glBindTexture(GL_TEXTURE_2D, texture_id);
        last_texture_id = texture_id;
    }
}

// Constructor initializes member variables
Renderer::Renderer(Context context) : m_context(context), m_xoffset(0), m_yoffset(0)
{
}

// Utility function to create Color object
Color Renderer::ToColor(int r, int g, int b, int a)
{
    Color c;
    c.r = r;
    c.g = g;
    c.b = b;
    c.a = a;
    return c;
}

// Set V-Sync interval based on platform
void Renderer::SetVSyncInterval(int interval)
{
#ifdef WIN32
    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (extensions && strstr(extensions, "WGL_EXT_swap_control") != nullptr) {
        typedef BOOL(APIENTRY* SWAP_INTERVAL_PROC)(int);
        SWAP_INTERVAL_PROC wglSwapIntervalEXT = reinterpret_cast<SWAP_INTERVAL_PROC>(wglGetProcAddress("wglSwapIntervalEXT"));
        if (wglSwapIntervalEXT) {
            wglSwapIntervalEXT(interval);
        }
    }
#else
    GLint i = interval;
    GLboolean ret = aglSetInteger(m_context, AGL_SWAP_INTERVAL, &i);
    if (ret == GL_FALSE) {
        // LOGTODO: Handle V-Sync not supported
    }
#endif
}

// Swap buffers based on platform
void Renderer::SwapBuffers()
{
#ifdef WIN32
    ::SwapBuffers(m_context);
#else
    aglSwapBuffers(m_context);
#endif
}

// Force texture binding, resetting last_texture_id
void Renderer::ForceTexture(unsigned int texture_id)
{
    last_texture_id = std::numeric_limits<unsigned int>::max();
    SelectTexture(texture_id);
}

// Set color using Color struct
void Renderer::SetColor(Color c)
{
    SetColor(c.r, c.g, c.b, c.a);
}

// Set color using individual RGBA components
void Renderer::SetColor(int r, int g, int b, int a)
{
    glColor4f(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

// Draw a simple quad with specified dimensions
void Renderer::DrawQuad(int x, int y, int w, int h)
{
    SelectTexture(0);
    glBegin(GL_QUADS);
    glVertex3i(x + m_xoffset, y + m_yoffset, 0);
    glVertex3i(x + w + m_xoffset, y + m_yoffset, 0);
    glVertex3i(x + w + m_xoffset, y + h + m_yoffset, 0);
    glVertex3i(x + m_xoffset, y + h + m_yoffset, 0);
    glEnd();
}

// Draw a TGA image with specified coordinates and dimensions
void Renderer::DrawTga(const Tga* tga, int x, int y) const
{
    DrawTga(tga, x, y, static_cast<int>(tga->GetWidth()), static_cast<int>(tga->GetHeight()), 0, 0);
}

// Draw a TGA image with specified coordinates, dimensions, and source position
void Renderer::DrawTga(const Tga* tga, int in_x, int in_y, int width, int height, int src_x, int src_y) const
{
    const int x = in_x + m_xoffset;
    const int y = in_y + m_yoffset;

    const double tx = static_cast<double>(src_x) / tga->GetWidth();
    const double ty = -static_cast<double>(src_y) / tga->GetHeight();
    const double tw = static_cast<double>(width) / tga->GetWidth();
    const double th = -static_cast<double>(height) / tga->GetHeight();

    SelectTexture(tga->GetId());

    glBegin(GL_QUADS);
    glTexCoord2d(tx, ty); glVertex3i(x, y, 0);
    glTexCoord2d(tx, ty + th); glVertex3i(x, y + height, 0);
    glTexCoord2d(tx + tw, ty + th); glVertex3i(x + width, y + height, 0);
    glTexCoord2d(tx + tw, ty); glVertex3i(x + width, y, 0);
    glEnd();
}

// Draw a stretched TGA image with specified coordinates and dimensions
void Renderer::DrawStretchedTga(const Tga* tga, int x, int y, int w, int h) const
{
    DrawStretchedTga(tga, x, y, w, h, 0, 0, static_cast<int>(tga->GetWidth()), static_cast<int>(tga->GetHeight()));
}

// Draw a stretched TGA image with specified coordinates, dimensions, source position, and source dimensions
void Renderer::DrawStretchedTga(const Tga* tga, int x, int y, int w, int h, int src_x, int src_y, int src_w, int src_h) const
{
    const int sx = x + m_xoffset;
    const int sy = y + m_yoffset;

    const double tx = static_cast<double>(src_x) / tga->GetWidth();
    const double ty = -static_cast<double>(src_y) / tga->GetHeight();
    const double tw = static_cast<double>(src_w) / tga->GetWidth();
    const double th = -static_cast<double>(src_h) / tga->GetHeight();

    SelectTexture(tga->GetId());

    glBegin(GL_QUADS);
    glTexCoord2d(tx, ty); glVertex3i(sx, sy, 0);
    glTexCoord2d(tx, ty + th); glVertex3i(sx, sy + h, 0);
    glTexCoord2d(tx + tw, ty + th); glVertex3i(sx + w, sy + h, 0);
    glTexCoord2d(tx + tw, ty); glVertex3i(sx + w, sy, 0);
    glEnd();
}
