//-----------------------------------------------------------------------------
// Copyright (c) 2008 dhpoware. All Rights Reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//-----------------------------------------------------------------------------
//
// This Windows Direct3D 9 application demonstrates per-pixel lighting using
// multiple point light sources. Lighting using multiple light sources can
// be performed in a single pass or with multiple passes that are additively
// blended together.
//
// Two versions of the per-pixel Blinn-Phong Direct3D Effect (.fx) file are
// provided: a shader model 2.0 version, and a shader model 3.0 version. Both
// versions implement per-pixel Blinn-Phong point lighting and supports
// attenuation (based on a light radius) and (Blinn-Phong) specular highlights.
//
// The shader model 2.0 version quickly reaches the 64 arithmetic instructions
// limit at 2 point lights in a single pass.
//
// Shader model 3.0 contains a loop instruction that greatly reduces the total
// number of instructions used. The shader model 3.0 version is hard coded to
// support a maximum of 8 lights in a single pass. However the shader is able
// to support more than this. This is a limit set by this demo.
//
// An alternative to single pass lighting is multi pass lighting. With multi
// pass lighting the scene is rendered once for each point light source. Each
// successive rendering pass after the first is additively blended together.
// This works well for less capable hardware that can only support shader model
// 2.0. But lighting using multiple passes is much more expensive compared to
// single pass lighting. For example, running this demo using only shader model
// 2.0 on a 2.4 GHz Intel Core2 Duo system with a NVIDIA G80 class video card
// saw a 33% drop in frame rate when switching from single pass lighting to
// multi pass lighting.
//
// The shader model 2.0 version supports both single pass and multi pass
// lighting. The shader model 3.0 version only supports single pass lighting.
//
//-----------------------------------------------------------------------------

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif

#if defined(_DEBUG)
#define D3D_DEBUG_INFO
#endif

#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <stdexcept>
#include <string>

#if defined(_DEBUG)
#include <crtdbg.h>
#endif

//-----------------------------------------------------------------------------
// Macros.
//-----------------------------------------------------------------------------

#define SAFE_RELEASE(x) if ((x) != 0) { (x)->Release(); (x) = 0; }

//-----------------------------------------------------------------------------
// Constants.
//-----------------------------------------------------------------------------

#if !defined(CLEARTYPE_QUALITY)
#define CLEARTYPE_QUALITY 5
#endif

#define APP_TITLE "D3D9 Multiple Point Lights Demo"

#if !defined(WHEEL_DELTA)
#define WHEEL_DELTA 120
#endif

#if !defined(WM_MOUSEWHEEL)
#define WM_MOUSEWHEEL 0x020A
#endif

const float CAMERA_FOVY = D3DXToRadian(45.0f);
const float CAMERA_ZNEAR = 0.01f;
const float CAMERA_ZFAR = 1000.0f;

const float MOUSE_ORBIT_SPEED = 0.3f;
const float MOUSE_DOLLY_SPEED = 1.0f;
const float MOUSE_TRACK_SPEED = 0.5f;
const float MOUSE_WHEEL_DOLLY_SPEED = 0.25f;

const float ROOM_SIZE_X = 256.0f;
const float ROOM_SIZE_Y = 128.0f;
const float ROOM_SIZE_Z = 256.0f;
const float ROOM_SIZE_X_HALF = ROOM_SIZE_X * 0.5f;
const float ROOM_SIZE_Y_HALF = ROOM_SIZE_Y * 0.5f;
const float ROOM_SIZE_Z_HALF = ROOM_SIZE_Z * 0.5f;

const float ROOM_WALL_TILE_U = 4.0f;
const float ROOM_WALL_TILE_V = 2.0f;
const float ROOM_FLOOR_TILE_U = 4.0f;
const float ROOM_FLOOR_TILE_V = 4.0f;
const float ROOM_CEILING_TILE_U = 4.0f;
const float ROOM_CEILING_TILE_V = 4.0f;

const float DOLLY_MAX = max(max(ROOM_SIZE_X, ROOM_SIZE_Y), ROOM_SIZE_Z) * 2.0f;
const float DOLLY_MIN = CAMERA_ZNEAR;

const int LIGHT_OBJECT_SLICES = 32;
const int LIGHT_OBJECT_STACKS = 32;
const float LIGHT_OBJECT_LAUNCH_ANGLE = 45.0f;
const float LIGHT_OBJECT_RADIUS = 2.0f;
const float LIGHT_OBJECT_SPEED = 80.0f;
const float LIGHT_RADIUS_MAX = max(max(ROOM_SIZE_X, ROOM_SIZE_Y), ROOM_SIZE_Z) * 1.25f;
const float LIGHT_RADIUS_MIN = 0.0f;

const int MAX_LIGHTS_SM20 = 2;
const int MAX_LIGHTS_SM30 = 8;

//-----------------------------------------------------------------------------
// Types.
//-----------------------------------------------------------------------------

struct Camera
{
    float pitch;
    float offset;
    D3DXVECTOR3 xAxis;
    D3DXVECTOR3 yAxis;
    D3DXVECTOR3 zAxis;
    D3DXVECTOR3 pos;
    D3DXVECTOR3 target;
    D3DXQUATERNION orientation;
    D3DXMATRIX viewProjectionMatrix;
};

struct Vertex
{
    float pos[3];
    float texCoord[2];
    float normal[3];
};

struct Material
{
	float ambient[4];
	float diffuse[4];
	float emissive[4];
	float specular[4];
	float shininess;
};

struct PointLight
{
	float pos[3];
	float ambient[4];
	float diffuse[4];
	float specular[4];
	float radius;
    D3DXVECTOR3 velocity;

    void init();
    void update(float elapsedTimeSec);
};

void PointLight::init()
{
    // Pick a random direction for the light to move along. We do this by
    // creating a random spherical coordinate and then convert that back to a
    // Cartesian coordinate. The point lights will always launch in some
    // upward direction at different speeds.

    float rho = LIGHT_OBJECT_SPEED + 0.5f * (LIGHT_OBJECT_SPEED * 
                (static_cast<float>(rand()) / static_cast<float>(RAND_MAX)));

    float phi = LIGHT_OBJECT_LAUNCH_ANGLE * (D3DX_PI / 180.0f);

    float theta = (360.0f * (static_cast<float>(rand()) / static_cast<float>(
                  RAND_MAX))) * (D3DX_PI / 180.0f);

    velocity.x = rho * cosf(phi) * cosf(theta);
    velocity.y = rho * sinf(phi);
    velocity.z = rho * cosf(phi) * sinf(theta);
}

void PointLight::update(float elapsedTimeSec)
{
    // Move the light.

    pos[0] += velocity.x * elapsedTimeSec;
    pos[1] += velocity.y * elapsedTimeSec;
    pos[2] += velocity.z * elapsedTimeSec;

    // Reflect the light off the sides of the room. This isn't true collision
    // detection and response. It's a bit of a hack but it seems to work well
    // enough for the purposes of this demo.

    if (pos[0] > (ROOM_SIZE_X_HALF - LIGHT_OBJECT_RADIUS * 2.0f))
        velocity.x = -velocity.x;

    if (pos[0] < -(ROOM_SIZE_X_HALF - LIGHT_OBJECT_RADIUS * 2.0f))
        velocity.x = -velocity.x;

    if (pos[1] > (ROOM_SIZE_Y_HALF - LIGHT_OBJECT_RADIUS * 2.0f))
        velocity.y = -velocity.y;

    if (pos[1] < -(ROOM_SIZE_Y_HALF - LIGHT_OBJECT_RADIUS * 2.0f))
        velocity.y = -velocity.y;

    if (pos[2] > (ROOM_SIZE_Z_HALF - LIGHT_OBJECT_RADIUS * 2.0f))
        velocity.z = -velocity.z;

    if (pos[2] < -(ROOM_SIZE_Z_HALF - LIGHT_OBJECT_RADIUS * 2.0f))
        velocity.z = -velocity.z;
}

//-----------------------------------------------------------------------------
// Globals.
//-----------------------------------------------------------------------------

HWND                         g_hWnd;
HINSTANCE                    g_hInstance;
D3DPRESENT_PARAMETERS        g_params;
IDirect3D9                  *g_pDirect3D;
IDirect3DDevice9            *g_pDevice;
ID3DXFont                   *g_pFont;
IDirect3DVertexDeclaration9 *g_pRoomVertexDecl;
IDirect3DVertexBuffer9      *g_pRoomVertexBuffer;
IDirect3DTexture9           *g_pNullTexture;
IDirect3DTexture9           *g_pWallColorTexture;
IDirect3DTexture9           *g_pCeilingColorTexture;
IDirect3DTexture9           *g_pFloorColorTexture;
ID3DXEffect                 *g_pBlinnPhongEffectSM20;
ID3DXEffect                 *g_pBlinnPhongEffectSM30;
ID3DXEffect                 *g_pBlinnPhongEffect;
ID3DXEffect                 *g_pAmbientEffect;
ID3DXMesh                   *g_pLightMesh;
D3DCAPS9                     g_caps;
bool                         g_enableVerticalSync;
bool                         g_isFullScreen;
bool                         g_hasFocus;
bool                         g_displayHelp;
bool                         g_disableColorMapTexture;
bool                         g_wireframe;
bool                         g_animateLights = true;
bool                         g_renderLights = true;
bool                         g_enableMultipassLighting;
bool                         g_supportsShaderModel30;
DWORD                        g_msaaSamples;
DWORD                        g_maxAnisotrophy;
int                          g_framesPerSecond;
int                          g_windowWidth;
int                          g_windowHeight;
int                          g_numLights;
float                        g_sceneAmbient[4] = {0.0f, 0.0f, 0.0f, 1.0f};

Camera g_camera =
{
    0.0f,
    ROOM_SIZE_Z,
    D3DXVECTOR3(1.0f, 0.0f, 0.0f),
    D3DXVECTOR3(0.0f, 1.0f, 0.0f),
    D3DXVECTOR3(0.0f, 0.0f, 1.0f),
    D3DXVECTOR3(0.0f, 0.0f, 0.0f),
    D3DXVECTOR3(0.0f, 0.0f, 0.0f),
    D3DXQUATERNION(0.0f, 0.0f, 0.0f, 1.0f),
    D3DXMATRIX()
};

PointLight g_lights[] =
{
    // 1st point light - WHITE
    {
        0.0f, 0.0f, 0.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
        100.0f,
        D3DXVECTOR3(0.0f, 0.0f, 0.0f)
    },

    // 2nd point light - RED
    {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
        100.0f,
        D3DXVECTOR3(0.0f, 0.0f, 0.0f)
    },

    // 3rd point light - GREEN
    {
        0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
        100.0f,
        D3DXVECTOR3(0.0f, 0.0f, 0.0f)
    },

    // 4th point light - BLUE
    {
        0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 1.0f,
        0.0f, 0.0f, 1.0f, 1.0f,
        0.0f, 0.0f, 1.0f, 1.0f,
        100.0f,
        D3DXVECTOR3(0.0f, 0.0f, 0.0f)
    },

    // 5th point light - YELLOW
    {
        0.0f, 0.0f, 0.0f,
        1.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 0.0f, 1.0f,
        100.0f,
        D3DXVECTOR3(0.0f, 0.0f, 0.0f)
    },

    // 6th point light - CYAN
    {
        0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 1.0f, 1.0f,
        0.0f, 1.0f, 1.0f, 1.0f,
        0.0f, 1.0f, 1.0f, 1.0f,
        100.0f,
        D3DXVECTOR3(0.0f, 0.0f, 0.0f)
    },

    // 7th point light - MAGENTA
    {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 1.0f, 1.0f,
        1.0f, 0.0f, 1.0f, 1.0f,
        1.0f, 0.0f, 1.0f, 1.0f,
        100.0f,
        D3DXVECTOR3(0.0f, 0.0f, 0.0f)
    },

    // 8th point light - CORNFLOWER BLUE
    {
        0.0f, 0.0f, 0.0f,
        (100.0f / 255.0f), (149.0f / 255.0f), (237.0f / 255.0f), 1.0f,
        (100.0f / 255.0f), (149.0f / 255.0f), (237.0f / 255.0f), 1.0f,
        (100.0f / 255.0f), (149.0f / 255.0f), (237.0f / 255.0f), 1.0f,
        100.0f,
        D3DXVECTOR3(0.0f, 0.0f, 0.0f)
    }
};

Material g_dullMaterial =
{
    0.2f, 0.2f, 0.2f, 1.0f,
    0.8f, 0.8f, 0.8f, 1.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
    0.0f
};

Material g_shinyMaterial =
{
    0.2f, 0.2f, 0.2f, 1.0f,
    0.8f, 0.8f, 0.8f, 1.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f,
    32.0f
};

D3DVERTEXELEMENT9 g_roomVertexElements[] =
{
    {0,  0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    {0, 20, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,   0},
    D3DDECL_END()
};

Vertex g_room[36] =
{
    // Wall: -Z face
    { ROOM_SIZE_X_HALF,  ROOM_SIZE_Y_HALF, -ROOM_SIZE_Z_HALF,    0.0f, 0.0f,                                0.0f,  0.0f,  1.0f},
    {-ROOM_SIZE_X_HALF,  ROOM_SIZE_Y_HALF, -ROOM_SIZE_Z_HALF,    ROOM_WALL_TILE_U, 0.0f,                    0.0f,  0.0f,  1.0f},
    {-ROOM_SIZE_X_HALF, -ROOM_SIZE_Y_HALF, -ROOM_SIZE_Z_HALF,    ROOM_WALL_TILE_U, ROOM_WALL_TILE_V,        0.0f,  0.0f,  1.0f},
    {-ROOM_SIZE_X_HALF, -ROOM_SIZE_Y_HALF, -ROOM_SIZE_Z_HALF,    ROOM_WALL_TILE_U, ROOM_WALL_TILE_V,        0.0f,  0.0f,  1.0f},
    { ROOM_SIZE_X_HALF, -ROOM_SIZE_Y_HALF, -ROOM_SIZE_Z_HALF,    0.0f, ROOM_WALL_TILE_V,                    0.0f,  0.0f,  1.0f},
    { ROOM_SIZE_X_HALF,  ROOM_SIZE_Y_HALF, -ROOM_SIZE_Z_HALF,    0.0f, 0.0f,                                0.0f,  0.0f,  1.0f},

    // Wall: +Z face
    {-ROOM_SIZE_X_HALF,  ROOM_SIZE_Y_HALF,  ROOM_SIZE_Z_HALF,    0.0f, 0.0f,                                0.0f,  0.0f, -1.0f},
    { ROOM_SIZE_X_HALF,  ROOM_SIZE_Y_HALF,  ROOM_SIZE_Z_HALF,    ROOM_WALL_TILE_U, 0.0f,                    0.0f,  0.0f, -1.0f},
    { ROOM_SIZE_X_HALF, -ROOM_SIZE_Y_HALF,  ROOM_SIZE_Z_HALF,    ROOM_WALL_TILE_U, ROOM_WALL_TILE_V,        0.0f,  0.0f, -1.0f},
    { ROOM_SIZE_X_HALF, -ROOM_SIZE_Y_HALF,  ROOM_SIZE_Z_HALF,    ROOM_WALL_TILE_U, ROOM_WALL_TILE_V,        0.0f,  0.0f, -1.0f},
    {-ROOM_SIZE_X_HALF, -ROOM_SIZE_Y_HALF,  ROOM_SIZE_Z_HALF,    0.0f, ROOM_WALL_TILE_V,                    0.0f,  0.0f, -1.0f},
    {-ROOM_SIZE_X_HALF,  ROOM_SIZE_Y_HALF,  ROOM_SIZE_Z_HALF,    0.0f, 0.0f,                                0.0f,  0.0f, -1.0f},

    // Wall: -X face
    {-ROOM_SIZE_X_HALF,  ROOM_SIZE_Y_HALF, -ROOM_SIZE_Z_HALF,    0.0f, 0.0f,                                1.0f,  0.0f,  0.0f},
    {-ROOM_SIZE_X_HALF,  ROOM_SIZE_Y_HALF,  ROOM_SIZE_Z_HALF,    ROOM_WALL_TILE_U, 0.0f,                    1.0f,  0.0f,  0.0f},
    {-ROOM_SIZE_X_HALF, -ROOM_SIZE_Y_HALF,  ROOM_SIZE_Z_HALF,    ROOM_WALL_TILE_U, ROOM_WALL_TILE_V,        1.0f,  0.0f,  0.0f},
    {-ROOM_SIZE_X_HALF, -ROOM_SIZE_Y_HALF,  ROOM_SIZE_Z_HALF,    ROOM_WALL_TILE_U, ROOM_WALL_TILE_V,        1.0f,  0.0f,  0.0f},
    {-ROOM_SIZE_X_HALF, -ROOM_SIZE_Y_HALF, -ROOM_SIZE_Z_HALF,    0.0f, ROOM_WALL_TILE_V,                    1.0f,  0.0f,  0.0f},
    {-ROOM_SIZE_X_HALF,  ROOM_SIZE_Y_HALF, -ROOM_SIZE_Z_HALF,    0.0f, 0.0f,                                1.0f,  0.0f,  0.0f},

    // Wall: +X face
    { ROOM_SIZE_X_HALF,  ROOM_SIZE_Y_HALF,  ROOM_SIZE_Z_HALF,    0.0f, 0.0f,                               -1.0f,  0.0f,  0.0f},
    { ROOM_SIZE_X_HALF,  ROOM_SIZE_Y_HALF, -ROOM_SIZE_Z_HALF,    ROOM_WALL_TILE_U, 0.0f,                   -1.0f,  0.0f,  0.0f},
    { ROOM_SIZE_X_HALF, -ROOM_SIZE_Y_HALF, -ROOM_SIZE_Z_HALF,    ROOM_WALL_TILE_U, ROOM_WALL_TILE_V,       -1.0f,  0.0f,  0.0f},
    { ROOM_SIZE_X_HALF, -ROOM_SIZE_Y_HALF, -ROOM_SIZE_Z_HALF,    ROOM_WALL_TILE_U, ROOM_WALL_TILE_V,       -1.0f,  0.0f,  0.0f},
    { ROOM_SIZE_X_HALF, -ROOM_SIZE_Y_HALF,  ROOM_SIZE_Z_HALF,    0.0f, ROOM_WALL_TILE_V,                   -1.0f,  0.0f,  0.0f},
    { ROOM_SIZE_X_HALF,  ROOM_SIZE_Y_HALF,  ROOM_SIZE_Z_HALF,    0.0f, 0.0f,                               -1.0f,  0.0f,  0.0f},

    // Ceiling: +Y face
    {-ROOM_SIZE_X_HALF,  ROOM_SIZE_Y_HALF, -ROOM_SIZE_Z_HALF,    0.0f, 0.0f,                                0.0f, -1.0f,  0.0f},
    { ROOM_SIZE_X_HALF,  ROOM_SIZE_Y_HALF, -ROOM_SIZE_Z_HALF,    ROOM_CEILING_TILE_U, 0.0f,                 0.0f, -1.0f,  0.0f},
    { ROOM_SIZE_X_HALF,  ROOM_SIZE_Y_HALF,  ROOM_SIZE_Z_HALF,    ROOM_CEILING_TILE_U, ROOM_CEILING_TILE_V,  0.0f, -1.0f,  0.0f},
    { ROOM_SIZE_X_HALF,  ROOM_SIZE_Y_HALF,  ROOM_SIZE_Z_HALF,    ROOM_CEILING_TILE_U, ROOM_CEILING_TILE_V,  0.0f, -1.0f,  0.0f},
    {-ROOM_SIZE_X_HALF,  ROOM_SIZE_Y_HALF,  ROOM_SIZE_Z_HALF,    0.0f, ROOM_CEILING_TILE_V,                 0.0f, -1.0f,  0.0f},
    {-ROOM_SIZE_X_HALF,  ROOM_SIZE_Y_HALF, -ROOM_SIZE_Z_HALF,    0.0f, 0.0f,                                0.0f, -1.0f,  0.0f},

    // Floor: -Y face
    {-ROOM_SIZE_X_HALF, -ROOM_SIZE_Y_HALF,  ROOM_SIZE_Z_HALF,    0.0f, 0.0f,                                0.0f,  1.0f,  0.0f},
    { ROOM_SIZE_X_HALF, -ROOM_SIZE_Y_HALF,  ROOM_SIZE_Z_HALF,    ROOM_FLOOR_TILE_U, 0.0f,                   0.0f,  1.0f,  0.0f},
    { ROOM_SIZE_X_HALF, -ROOM_SIZE_Y_HALF, -ROOM_SIZE_Z_HALF,    ROOM_FLOOR_TILE_U, ROOM_FLOOR_TILE_V,      0.0f,  1.0f,  0.0f},
    { ROOM_SIZE_X_HALF, -ROOM_SIZE_Y_HALF, -ROOM_SIZE_Z_HALF,    ROOM_FLOOR_TILE_U, ROOM_FLOOR_TILE_V,      0.0f,  1.0f,  0.0f},
    {-ROOM_SIZE_X_HALF, -ROOM_SIZE_Y_HALF, -ROOM_SIZE_Z_HALF,    0.0f, ROOM_FLOOR_TILE_V,                   0.0f,  1.0f,  0.0f},
    {-ROOM_SIZE_X_HALF, -ROOM_SIZE_Y_HALF,  ROOM_SIZE_Z_HALF,    0.0f, 0.0f,                                0.0f,  1.0f,  0.0f}
};

//-----------------------------------------------------------------------------
// Function Prototypes.
//-----------------------------------------------------------------------------

void    ChooseBestMSAAMode(D3DFORMAT backBufferFmt, D3DFORMAT depthStencilFmt,
                           BOOL windowed, D3DMULTISAMPLE_TYPE &type,
                           DWORD &qualityLevels, DWORD &samplesPerPixel);
void    Cleanup();
void    CleanupApp();
HWND    CreateAppWindow(const WNDCLASSEX &wcl, const char *pszTitle);
bool    CreateNullTexture(int width, int height, LPDIRECT3DTEXTURE9 &pTexture);
bool    DeviceIsValid();
float   GetElapsedTimeInSeconds();
bool    Init();
void    InitApp();
bool    InitD3D();
bool    InitFont(const char *pszFont, int ptSize, LPD3DXFONT &pFont);
void    InitRoom();
bool    LoadShader(const char *pszFilename, LPD3DXEFFECT &pEffect);
void    Log(const char *pszMessage);
bool    MSAAModeSupported(D3DMULTISAMPLE_TYPE type, D3DFORMAT backBufferFmt,
                          D3DFORMAT depthStencilFmt, BOOL windowed,
                          DWORD &qualityLevels);
void    ProcessMouseInput(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void    RenderFrame();
void    RenderRoomUsingBlinnPhong();
void    RenderLight(int i);
void    RenderText();
bool    ResetDevice();
void    SetProcessorAffinity();
void    ToggleFullScreen();
void    UpdateFrame(float elapsedTimeSec);
void    UpdateFrameRate(float elapsedTimeSec);
void    UpdateEffects();
void    UpdateLights(float elapsedTimeSec);
LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

//-----------------------------------------------------------------------------
// Functions.
//-----------------------------------------------------------------------------

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
#if defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_LEAK_CHECK_DF | _CRTDBG_ALLOC_MEM_DF);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    MSG msg = {0};
    WNDCLASSEX wcl = {0};

    wcl.cbSize = sizeof(wcl);
    wcl.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wcl.lpfnWndProc = WindowProc;
    wcl.cbClsExtra = 0;
    wcl.cbWndExtra = 0;
    wcl.hInstance = g_hInstance = hInstance;
    wcl.hIcon = LoadIcon(0, IDI_APPLICATION);
    wcl.hCursor = LoadCursor(0, IDC_ARROW);
    wcl.hbrBackground = 0;
    wcl.lpszMenuName = 0;
    wcl.lpszClassName = "D3D9WindowClass";
    wcl.hIconSm = 0;

    if (!RegisterClassEx(&wcl))
        return 0;

    g_hWnd = CreateAppWindow(wcl, APP_TITLE);

    if (g_hWnd)
    {
        SetProcessorAffinity();

        if (Init())
        {
            ShowWindow(g_hWnd, nShowCmd);
            UpdateWindow(g_hWnd);

            while (true)
            {
                while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
                {
                    if (msg.message == WM_QUIT)
                        break;

                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }

                if (msg.message == WM_QUIT)
                    break;

                if (g_hasFocus)
                {
                    UpdateFrame(GetElapsedTimeInSeconds());

                    if (DeviceIsValid())
                        RenderFrame();
                }
                else
                {
                    WaitMessage();
                }
            }
        }

        Cleanup();
        UnregisterClass(wcl.lpszClassName, hInstance);
    }

    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ACTIVATE:
        switch (wParam)
        {
        default:
            break;

        case WA_ACTIVE:
        case WA_CLICKACTIVE:
            g_hasFocus = true;
            break;

        case WA_INACTIVE:
            if (g_isFullScreen)
                ShowWindow(hWnd, SW_MINIMIZE);
            g_hasFocus = false;
            break;
        }
        break;

    case WM_CHAR:
        switch (static_cast<int>(wParam))
        {
        case VK_ESCAPE:
            PostMessage(hWnd, WM_CLOSE, 0, 0);
            break;

        case VK_SPACE:
            g_animateLights = !g_animateLights;
            break;

        case '+':
        case '=':
            for (int i = 0; i < sizeof(g_lights) / sizeof(g_lights[0]); ++i)
            {
                if ((g_lights[i].radius += 1.0f) > LIGHT_RADIUS_MAX)
                    g_lights[i].radius = LIGHT_RADIUS_MAX;
            }
            break;

        case '-':
            for (int i = 0; i < sizeof(g_lights) / sizeof(g_lights[0]); ++i)
            {
                if ((g_lights[i].radius -= 1.0f) < LIGHT_RADIUS_MIN)
                    g_lights[i].radius = LIGHT_RADIUS_MIN;
            }
            break;

        case 'h':
        case 'H':
            g_displayHelp = !g_displayHelp;
            break;

        case 'l':
        case 'L':
            g_renderLights = !g_renderLights;
            break;

        case 'm':
        case 'M':
            if (g_pBlinnPhongEffect == g_pBlinnPhongEffectSM20)
                g_enableMultipassLighting = !g_enableMultipassLighting;
            break;

        case 's':
        case 'S':
            if (g_supportsShaderModel30)
            {
                if (g_pBlinnPhongEffect == g_pBlinnPhongEffectSM20)
                {
                    g_pBlinnPhongEffect = g_pBlinnPhongEffectSM30;
                    g_numLights = MAX_LIGHTS_SM30;
                }
                else
                {
                    g_pBlinnPhongEffect = g_pBlinnPhongEffectSM20;
                    g_numLights = MAX_LIGHTS_SM20;
                }
            }
            break;

        case 'T':
        case 't':
            g_disableColorMapTexture = !g_disableColorMapTexture;
            break;

        default:
            break;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_SIZE:
        g_windowWidth = static_cast<int>(LOWORD(lParam));
        g_windowHeight = static_cast<int>(HIWORD(lParam));
        break;

    case WM_SYSKEYDOWN:
        if (wParam == VK_RETURN)
            ToggleFullScreen();
        break;

    default:
        ProcessMouseInput(hWnd, msg, wParam, lParam);
        break;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

void ChooseBestMSAAMode(D3DFORMAT backBufferFmt, D3DFORMAT depthStencilFmt,
                        BOOL windowed, D3DMULTISAMPLE_TYPE &type,
                        DWORD &qualityLevels, DWORD &samplesPerPixel)
{
    bool supported = false;

    struct MSAAMode
    {
        D3DMULTISAMPLE_TYPE type;
        DWORD samples;
    }
    multsamplingTypes[15] =
    {
        { D3DMULTISAMPLE_16_SAMPLES,  16 },
        { D3DMULTISAMPLE_15_SAMPLES,  15 },
        { D3DMULTISAMPLE_14_SAMPLES,  14 },
        { D3DMULTISAMPLE_13_SAMPLES,  13 },
        { D3DMULTISAMPLE_12_SAMPLES,  12 },
        { D3DMULTISAMPLE_11_SAMPLES,  11 },
        { D3DMULTISAMPLE_10_SAMPLES,  10 },
        { D3DMULTISAMPLE_9_SAMPLES,   9  },
        { D3DMULTISAMPLE_8_SAMPLES,   8  },
        { D3DMULTISAMPLE_7_SAMPLES,   7  },
        { D3DMULTISAMPLE_6_SAMPLES,   6  },
        { D3DMULTISAMPLE_5_SAMPLES,   5  },
        { D3DMULTISAMPLE_4_SAMPLES,   4  },
        { D3DMULTISAMPLE_3_SAMPLES,   3  },
        { D3DMULTISAMPLE_2_SAMPLES,   2  }
    };

    for (int i = 0; i < 15; ++i)
    {
        type = multsamplingTypes[i].type;

        supported = MSAAModeSupported(type, backBufferFmt, depthStencilFmt,
                        windowed, qualityLevels);

        if (supported)
        {
            samplesPerPixel = multsamplingTypes[i].samples;
            return;
        }
    }

    type = D3DMULTISAMPLE_NONE;
    qualityLevels = 0;
    samplesPerPixel = 1;
}

void Cleanup()
{
    CleanupApp();

    SAFE_RELEASE(g_pNullTexture);
    SAFE_RELEASE(g_pFont);
    SAFE_RELEASE(g_pDevice);
    SAFE_RELEASE(g_pDirect3D);
}

void CleanupApp()
{
    SAFE_RELEASE(g_pAmbientEffect);
    SAFE_RELEASE(g_pBlinnPhongEffectSM20);
    SAFE_RELEASE(g_pBlinnPhongEffectSM30);
    SAFE_RELEASE(g_pWallColorTexture);
    SAFE_RELEASE(g_pCeilingColorTexture);
    SAFE_RELEASE(g_pFloorColorTexture);
    SAFE_RELEASE(g_pRoomVertexBuffer);
    SAFE_RELEASE(g_pRoomVertexDecl);
    SAFE_RELEASE(g_pLightMesh);
}

HWND CreateAppWindow(const WNDCLASSEX &wcl, const char *pszTitle)
{
    // Create a window that is centered on the desktop. It's exactly 1/4 the
    // size of the desktop. Don't allow it to be resized.

    DWORD wndExStyle = WS_EX_OVERLAPPEDWINDOW;
    DWORD wndStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                     WS_MINIMIZEBOX | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;

    HWND hWnd = CreateWindowEx(wndExStyle, wcl.lpszClassName, pszTitle,
                    wndStyle, 0, 0, 0, 0, 0, 0, wcl.hInstance, 0);

    if (hWnd)
    {
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int halfScreenWidth = screenWidth / 2;
        int halfScreenHeight = screenHeight / 2;
        int left = (screenWidth - halfScreenWidth) / 2;
        int top = (screenHeight - halfScreenHeight) / 2;
        RECT rc = {0};

        SetRect(&rc, left, top, left + halfScreenWidth, top + halfScreenHeight);
        AdjustWindowRectEx(&rc, wndStyle, FALSE, wndExStyle);
        MoveWindow(hWnd, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, TRUE);

        GetClientRect(hWnd, &rc);
        g_windowWidth = rc.right - rc.left;
        g_windowHeight = rc.bottom - rc.top;
    }

    return hWnd;
}

bool CreateNullTexture(int width, int height, LPDIRECT3DTEXTURE9 &pTexture)
{
    // Create an empty white texture. This texture is applied to geometry
    // that doesn't have any texture maps. This trick allows the same shader to
    // be used to draw the geometry with and without textures applied.

    HRESULT hr = D3DXCreateTexture(g_pDevice, width, height, 0, 0,
                    D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &pTexture);

    if (FAILED(hr))
        return false;

    LPDIRECT3DSURFACE9 pSurface = 0;

    if (SUCCEEDED(pTexture->GetSurfaceLevel(0, &pSurface)))
    {
        D3DLOCKED_RECT rcLock = {0};

        if (SUCCEEDED(pSurface->LockRect(&rcLock, 0, 0)))
        {
            BYTE *pPixels = static_cast<BYTE*>(rcLock.pBits);
            int widthInBytes = width * 4;

            if (widthInBytes == rcLock.Pitch)
            {
                memset(pPixels, 0xff, widthInBytes * height);
            }
            else
            {
                for (int y = 0; y < height; ++y)
                    memset(&pPixels[y * rcLock.Pitch], 0xff, rcLock.Pitch);
            }

            pSurface->UnlockRect();
            pSurface->Release();
            return true;
        }

        pSurface->Release();
    }

    pTexture->Release();
    return false;
}

bool DeviceIsValid()
{
    HRESULT hr = g_pDevice->TestCooperativeLevel();

    if (FAILED(hr))
    {
        if (hr == D3DERR_DEVICENOTRESET)
            return ResetDevice();
    }

    return true;
}

float GetElapsedTimeInSeconds()
{
    // Returns the elapsed time (in seconds) since the last time this function
    // was called. This elaborate setup is to guard against large spikes in
    // the time returned by QueryPerformanceCounter().

    static const int MAX_SAMPLE_COUNT = 50;

    static float frameTimes[MAX_SAMPLE_COUNT];
    static float timeScale = 0.0f;
    static float actualElapsedTimeSec = 0.0f;
    static INT64 freq = 0;
    static INT64 lastTime = 0;
    static int sampleCount = 0;
    static bool initialized = false;

    INT64 time = 0;
    float elapsedTimeSec = 0.0f;

    if (!initialized)
    {
        initialized = true;
        QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER*>(&freq));
        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&lastTime));
        timeScale = 1.0f / freq;
    }

    QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&time));
    elapsedTimeSec = (time - lastTime) * timeScale;
    lastTime = time;

    if (fabsf(elapsedTimeSec - actualElapsedTimeSec) < 1.0f)
    {
        memmove(&frameTimes[1], frameTimes, sizeof(frameTimes) - sizeof(frameTimes[0]));
        frameTimes[0] = elapsedTimeSec;

        if (sampleCount < MAX_SAMPLE_COUNT)
            ++sampleCount;
    }

    actualElapsedTimeSec = 0.0f;

    for (int i = 0; i < sampleCount; ++i)
        actualElapsedTimeSec += frameTimes[i];

    if (sampleCount > 0)
        actualElapsedTimeSec /= sampleCount;

    return actualElapsedTimeSec;
}

bool Init()
{
    if (!InitD3D())
    {
        Log("Direct3D initialization failed!");
        return false;
    }

    try
    {
        InitApp();
        return true;
    }
    catch (const std::exception &e)
    {
        std::ostringstream msg;

        msg << "Application initialization failed!" << std::endl << std::endl;
        msg << e.what();

        Log(msg.str().c_str());
        return false;
    }
}

void InitApp()
{
    // Verify that shader model 2.0 or higher is supported.

    DWORD dwVSVersion = g_caps.VertexShaderVersion;
    DWORD dwPSVersion = g_caps.PixelShaderVersion;

    if (dwVSVersion >= D3DVS_VERSION(3,0) && dwPSVersion >= D3DPS_VERSION(3,0))
        g_supportsShaderModel30 = true;
    else if (dwVSVersion >= D3DVS_VERSION(2,0) && dwPSVersion >= D3DPS_VERSION(2,0))
        g_supportsShaderModel30 = false;
    else
        throw std::runtime_error("Shader model 2.0 or higher is required.");

    // Setup fonts.

    if (!InitFont("Arial", 10, g_pFont))
        throw std::runtime_error("Failed to create font.");

    // Load shaders.

    if (!LoadShader("Content/Shaders/ambient.fx", g_pAmbientEffect))
        throw std::runtime_error("Failed to load shader: ambient.fx.");

    if (!LoadShader("Content/Shaders/blinn_phong_sm20.fx", g_pBlinnPhongEffectSM20))
        throw std::runtime_error("Failed to load shader: blinn_phong_sm20.fx.");

    if (g_supportsShaderModel30)
    {
        if (!LoadShader("Content/Shaders/blinn_phong_sm30.fx", g_pBlinnPhongEffectSM30))
            throw std::runtime_error("Failed to load shader: blinn_phong_sm30.fx.");

        g_pBlinnPhongEffect = g_pBlinnPhongEffectSM30;
        g_numLights = MAX_LIGHTS_SM30;
    }
    else
    {
        g_pBlinnPhongEffect = g_pBlinnPhongEffectSM20;
        g_numLights = MAX_LIGHTS_SM20;
    }
    
    // Load textures.

    if (!CreateNullTexture(2, 2, g_pNullTexture))
        throw std::runtime_error("Failed to create null texture.");

    if (FAILED(D3DXCreateTextureFromFile(g_pDevice,
            "Content/Textures/brick_color_map.jpg", &g_pWallColorTexture)))
        throw std::runtime_error("Failed to load texture: brick_color_map.jpg.");

    if (FAILED(D3DXCreateTextureFromFile(g_pDevice,
            "Content/Textures/wood_color_map.jpg", &g_pCeilingColorTexture)))
        throw std::runtime_error("Failed to load texture: wood_color_map.jpg.");

    if (FAILED(D3DXCreateTextureFromFile(g_pDevice,
            "Content/Textures/stone_color_map.jpg", &g_pFloorColorTexture)))
        throw std::runtime_error("Failed to load texture: stone_color_map.jpg.");

    // Create geometry for the room.

    InitRoom();

    // Create geometry for the light.

    if (FAILED(D3DXCreateSphere(g_pDevice, LIGHT_OBJECT_RADIUS,
            LIGHT_OBJECT_SLICES, LIGHT_OBJECT_STACKS, &g_pLightMesh, 0)))
        throw std::runtime_error("Failed to create the point light mesh.");

    // Seed the random number generator.

    srand(GetTickCount());

    // Initialize the point lights in the scene.

    for (int i = 0; i < g_numLights; ++i)
        g_lights[i].init();
}

bool InitD3D()
{
    HRESULT hr = 0;
    D3DDISPLAYMODE desktop = {0};

    g_pDirect3D = Direct3DCreate9(D3D_SDK_VERSION);

    if (!g_pDirect3D)
        return false;

    // Just use the current desktop display mode.
    hr = g_pDirect3D->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &desktop);

    if (FAILED(hr))
    {
        g_pDirect3D->Release();
        g_pDirect3D = 0;
        return false;
    }

    // Setup Direct3D for windowed rendering.
    g_params.BackBufferWidth = 0;
    g_params.BackBufferHeight = 0;
    g_params.BackBufferFormat = desktop.Format;
    g_params.BackBufferCount = 1;
    g_params.hDeviceWindow = g_hWnd;
    g_params.Windowed = TRUE;
    g_params.EnableAutoDepthStencil = TRUE;
    g_params.AutoDepthStencilFormat = D3DFMT_D24S8;
    g_params.Flags = D3DPRESENTFLAG_DISCARD_DEPTHSTENCIL;
    g_params.FullScreen_RefreshRateInHz = 0;

    if (g_enableVerticalSync)
        g_params.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
    else
        g_params.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    // Swap effect must be D3DSWAPEFFECT_DISCARD for multi-sampling support.
    g_params.SwapEffect = D3DSWAPEFFECT_DISCARD;

    // Select the highest quality multi-sample anti-aliasing (MSAA) mode.
    ChooseBestMSAAMode(g_params.BackBufferFormat, g_params.AutoDepthStencilFormat,
        g_params.Windowed, g_params.MultiSampleType, g_params.MultiSampleQuality,
        g_msaaSamples);

    // Most modern video cards should have no problems creating pure devices.
    // Note that by creating a pure device we lose the ability to debug vertex
    // and pixel shaders.
    hr = g_pDirect3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_hWnd,
            D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE,
            &g_params, &g_pDevice);

    if (FAILED(hr))
    {
        // Fall back to software vertex processing for less capable hardware.
        // Note that in order to debug vertex shaders we must use a software
        // vertex processing device.
        hr = g_pDirect3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_hWnd,
                D3DCREATE_SOFTWARE_VERTEXPROCESSING, &g_params, &g_pDevice);
    }

    if (FAILED(hr))
    {
        g_pDirect3D->Release();
        g_pDirect3D = 0;
        return false;
    }   

    if (SUCCEEDED(g_pDevice->GetDeviceCaps(&g_caps)))
    {
        // Prefer anisotropic texture filtering if it's supported.
        if (g_caps.RasterCaps & D3DPRASTERCAPS_ANISOTROPY)
            g_maxAnisotrophy = g_caps.MaxAnisotropy;
        else
            g_maxAnisotrophy = 1;
    }

    return true;
}

bool InitFont(const char *pszFont, int ptSize, LPD3DXFONT &pFont)
{
    static DWORD dwQuality = 0;

    // Prefer ClearType font quality if available.

    if (!dwQuality)
    {
        DWORD dwVersion = GetVersion();
        DWORD dwMajorVersion = static_cast<DWORD>((LOBYTE(LOWORD(dwVersion))));
        DWORD dwMinorVersion = static_cast<DWORD>((HIBYTE(LOWORD(dwVersion))));

        // Windows XP and higher will support ClearType quality fonts.
        if (dwMajorVersion >= 6 || (dwMajorVersion == 5 && dwMinorVersion == 1))
            dwQuality = CLEARTYPE_QUALITY;
        else
            dwQuality = ANTIALIASED_QUALITY;
    }

    int logPixelsY = 0;

    // Convert from font point size to pixel size.

    if (HDC hDC = GetDC((0)))
    {
        logPixelsY = GetDeviceCaps(hDC, LOGPIXELSY);
        ReleaseDC(0, hDC);
    }

    int fontCharHeight = -ptSize * logPixelsY / 72;

    // Now create the font. Prefer anti-aliased text.

    HRESULT hr = D3DXCreateFont(
        g_pDevice,
        fontCharHeight,                 // height
        0,                              // width
        FW_BOLD,                        // weight
        1,                              // mipmap levels
        FALSE,                          // italic
        DEFAULT_CHARSET,                // char set
        OUT_DEFAULT_PRECIS,             // output precision
        dwQuality,                      // quality
        DEFAULT_PITCH | FF_DONTCARE,    // pitch and family
        pszFont,                        // font name
        &pFont);

    return SUCCEEDED(hr) ? true : false;
}

void InitRoom()
{
    if (FAILED(g_pDevice->CreateVertexDeclaration(g_roomVertexElements, &g_pRoomVertexDecl)))
        throw std::runtime_error("Failed to create vertex declaration for room.");

    if (FAILED(g_pDevice->CreateVertexBuffer(sizeof(Vertex) * 36, 0, 0,
            D3DPOOL_MANAGED, &g_pRoomVertexBuffer, 0)))
        throw std::runtime_error("Failed to create vertex buffer for room.");

    Vertex *pVertices = 0;

    if (FAILED(g_pRoomVertexBuffer->Lock(0, 0, reinterpret_cast<void**>(&pVertices), 0)))
        throw std::runtime_error("Failed to lock room vertex buffer.");

    memcpy(pVertices, g_room, sizeof(g_room));
    g_pRoomVertexBuffer->Unlock();
}

bool LoadShader(const char *pszFilename, LPD3DXEFFECT &pEffect)
{
    ID3DXBuffer *pCompilationErrors = 0;
    DWORD dwShaderFlags = D3DXFX_NOT_CLONEABLE | D3DXSHADER_NO_PRESHADER;

    // Both vertex and pixel shaders can be debugged. To enable shader
    // debugging add the following flag to the dwShaderFlags variable:
    //      dwShaderFlags |= D3DXSHADER_DEBUG;
    //
    // Vertex shaders can be debugged with either the REF device or a device
    // created for software vertex processing (i.e., the IDirect3DDevice9
    // object must be created with the D3DCREATE_SOFTWARE_VERTEXPROCESSING
    // behavior). Pixel shaders can be debugged only using the REF device.
    //
    // To enable vertex shader debugging add the following flag to the
    // dwShaderFlags variable:
    //     dwShaderFlags |= D3DXSHADER_FORCE_VS_SOFTWARE_NOOPT;
    //
    // To enable pixel shader debugging add the following flag to the
    // dwShaderFlags variable:
    //     dwShaderFlags |= D3DXSHADER_FORCE_PS_SOFTWARE_NOOPT;

    HRESULT hr = D3DXCreateEffectFromFile(g_pDevice, pszFilename, 0, 0,
                    dwShaderFlags, 0, &pEffect, &pCompilationErrors);

    if (FAILED(hr))
    {
        if (pCompilationErrors)
        {
            std::string compilationErrors(static_cast<const char *>(
                            pCompilationErrors->GetBufferPointer()));

            pCompilationErrors->Release();
            throw std::runtime_error(compilationErrors);
        }
    }

    if (pCompilationErrors)
        pCompilationErrors->Release();

    return pEffect != 0;
}

void Log(const char *pszMessage)
{
    MessageBox(0, pszMessage, "Error", MB_ICONSTOP);
}

bool MSAAModeSupported(D3DMULTISAMPLE_TYPE type, D3DFORMAT backBufferFmt,
                       D3DFORMAT depthStencilFmt, BOOL windowed,
                       DWORD &qualityLevels)
{
    DWORD backBufferQualityLevels = 0;
    DWORD depthStencilQualityLevels = 0;

    HRESULT hr = g_pDirect3D->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT,
                    D3DDEVTYPE_HAL, backBufferFmt, windowed, type,
                    &backBufferQualityLevels);

    if (SUCCEEDED(hr))
    {
        hr = g_pDirect3D->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT,
                D3DDEVTYPE_HAL, depthStencilFmt, windowed, type,
                &depthStencilQualityLevels);

        if (SUCCEEDED(hr))
        {
            if (backBufferQualityLevels == depthStencilQualityLevels)
            {
                // The valid range is between zero and one less than the level
                // returned by IDirect3D9::CheckDeviceMultiSampleType().

                if (backBufferQualityLevels > 0)
                    qualityLevels = backBufferQualityLevels - 1;
                else
                    qualityLevels = backBufferQualityLevels;

                return true;
            }
        }
    }

    return false;
}

void ProcessMouseInput(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Use the left mouse button to track the camera.
    // Use the middle mouse button to dolly the camera.
    // Use the right mouse button to orbit the camera.

    enum CameraMode {CAMERA_NONE, CAMERA_TRACK, CAMERA_DOLLY, CAMERA_ORBIT};

    static CameraMode cameraMode = CAMERA_NONE;
    static POINT ptMousePrev = {0};
    static POINT ptMouseCurrent = {0};
    static int mouseButtonsDown = 0;
    static float dx = 0.0f;
    static float dy = 0.0f;
    static float wheelDelta = 0.0f;
    static D3DXVECTOR3 xAxis(1.0f, 0.0f, 0.0f);
    static D3DXVECTOR3 yAxis(0.0f, 1.0f, 0.0f);
    static D3DXQUATERNION temp;

    switch (msg)
    {
    case WM_LBUTTONDOWN:
        cameraMode = CAMERA_TRACK;
        ++mouseButtonsDown;
        SetCapture(hWnd);
        ptMousePrev.x = static_cast<int>(static_cast<short>(LOWORD(lParam)));
        ptMousePrev.y = static_cast<int>(static_cast<short>(HIWORD(lParam)));
        ClientToScreen(hWnd, &ptMousePrev);
        break;

    case WM_RBUTTONDOWN:
        cameraMode = CAMERA_ORBIT;
        ++mouseButtonsDown;
        SetCapture(hWnd);
        ptMousePrev.x = static_cast<int>(static_cast<short>(LOWORD(lParam)));
        ptMousePrev.y = static_cast<int>(static_cast<short>(HIWORD(lParam)));
        ClientToScreen(hWnd, &ptMousePrev);
        break;

    case WM_MBUTTONDOWN:
        cameraMode = CAMERA_DOLLY;
        ++mouseButtonsDown;
        SetCapture(hWnd);
        ptMousePrev.x = static_cast<int>(static_cast<short>(LOWORD(lParam)));
        ptMousePrev.y = static_cast<int>(static_cast<short>(HIWORD(lParam)));
        ClientToScreen(hWnd, &ptMousePrev);
        break;

    case WM_MOUSEMOVE:
        ptMouseCurrent.x = static_cast<int>(static_cast<short>(LOWORD(lParam)));
        ptMouseCurrent.y = static_cast<int>(static_cast<short>(HIWORD(lParam)));
        ClientToScreen(hWnd, &ptMouseCurrent);

        switch (cameraMode)
        {
        case CAMERA_TRACK:
            dx = static_cast<float>(ptMouseCurrent.x - ptMousePrev.x);
            dx *= MOUSE_TRACK_SPEED;

            dy = static_cast<float>(ptMouseCurrent.y - ptMousePrev.y);
            dy *= MOUSE_TRACK_SPEED;

            g_camera.target -= g_camera.xAxis * dx;
            g_camera.target += g_camera.yAxis * dy;
    
            break;

        case CAMERA_DOLLY:
            dy = static_cast<float>(ptMousePrev.y - ptMouseCurrent.y);
            dy *= MOUSE_DOLLY_SPEED;

            g_camera.offset -= dy;

            if (g_camera.offset > DOLLY_MAX)
                g_camera.offset = DOLLY_MAX;

            if (g_camera.offset < DOLLY_MIN)
                g_camera.offset = DOLLY_MIN;

            break;

        case CAMERA_ORBIT:
            dx = static_cast<float>(ptMousePrev.x - ptMouseCurrent.x);
            dx *= MOUSE_ORBIT_SPEED;

            dy = static_cast<float>(ptMousePrev.y - ptMouseCurrent.y);
            dy *= MOUSE_ORBIT_SPEED;

            g_camera.pitch += dy;

            if (g_camera.pitch > 90.0f)
            {
                dy = 90.0f - (g_camera.pitch - dy);
                g_camera.pitch = 90.0f;
            }

            if (g_camera.pitch < -90.0f)
            {
                dy = -90.0f - (g_camera.pitch - dy);
                g_camera.pitch = -90.0f;
            }

            dx = D3DXToRadian(dx);
            dy = D3DXToRadian(dy);
            
            if (dx != 0.0f)
            {
                D3DXQuaternionRotationAxis(&temp, &yAxis, dx);
                D3DXQuaternionMultiply(&g_camera.orientation, &temp, &g_camera.orientation);
            }

            if (dy != 0.0f)
            {
                D3DXQuaternionRotationAxis(&temp, &xAxis, dy);
                D3DXQuaternionMultiply(&g_camera.orientation, &g_camera.orientation, &temp);
            }

            break;
        }

        ptMousePrev.x = ptMouseCurrent.x;
        ptMousePrev.y = ptMouseCurrent.y;
        break;

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
        if (--mouseButtonsDown <= 0)
        {
            mouseButtonsDown = 0;
            cameraMode = CAMERA_NONE;
            ReleaseCapture();
        }
        else
        {
            if (wParam & MK_LBUTTON)
                cameraMode = CAMERA_TRACK;
            else if (wParam & MK_RBUTTON)
                cameraMode = CAMERA_ORBIT;
            else if (wParam & MK_MBUTTON)
                cameraMode = CAMERA_DOLLY;
        }
        break;

    case WM_MOUSEWHEEL:
        wheelDelta = static_cast<float>(static_cast<int>(wParam) >> 16);
        g_camera.offset -= wheelDelta * MOUSE_WHEEL_DOLLY_SPEED;

        if (g_camera.offset > DOLLY_MAX)
            g_camera.offset = DOLLY_MAX;

        if (g_camera.offset < DOLLY_MIN)
            g_camera.offset = DOLLY_MIN;

        break;

    default:
        break;
    }
}

void RenderFrame()
{
    g_pDevice->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.0f, 0);

    if (FAILED(g_pDevice->BeginScene()))
        return;

    RenderRoomUsingBlinnPhong();

    if (g_renderLights)
    {
        for (int i = 0; i < g_numLights; ++i)
            RenderLight(i);
    }

    RenderText();

    g_pDevice->EndScene();
    g_pDevice->Present(0, 0, 0, 0);
}

void RenderLight(int i)
{
    static UINT totalPasses;
    static D3DXHANDLE hTechnique;
    static D3DXMATRIX world;
    static D3DXMATRIX worldViewProjection;

    hTechnique = g_pAmbientEffect->GetTechniqueByName("AmbientLighting");

    if (FAILED(g_pAmbientEffect->SetTechnique(hTechnique)))
        return;

    D3DXMatrixTranslation(&world, g_lights[i].pos[0], g_lights[i].pos[1], g_lights[i].pos[2]);
    worldViewProjection = world * g_camera.viewProjectionMatrix;

    g_pAmbientEffect->SetMatrix("worldViewProjectionMatrix", &worldViewProjection);
    g_pAmbientEffect->SetFloat("ambientIntensity", 1.0f);
    g_pAmbientEffect->SetValue("ambientColor", g_lights[i].ambient, sizeof(g_lights[i].ambient));

    // Draw the light object.

    if (SUCCEEDED(g_pAmbientEffect->Begin(&totalPasses, 0)))
    {
        for (UINT pass = 0; pass < totalPasses; ++pass)
        {
            if (SUCCEEDED(g_pAmbientEffect->BeginPass(pass)))
            {
                g_pLightMesh->DrawSubset(0);
                g_pAmbientEffect->EndPass();
            }
        }

        g_pAmbientEffect->End();
    }
}

void RenderRoomUsingBlinnPhong()
{
    static UINT totalPasses;
    static D3DXHANDLE hTechnique;

    if (g_pBlinnPhongEffect == g_pBlinnPhongEffectSM30)
    {
        hTechnique = g_pBlinnPhongEffect->GetTechniqueByName("PerPixelPointLighting");
    }
    else
    {
        if (g_enableMultipassLighting)
            hTechnique = g_pBlinnPhongEffect->GetTechniqueByName("PerPixelPointLightingMultiPass");
        else
            hTechnique = g_pBlinnPhongEffect->GetTechniqueByName("PerPixelPointLightingSinglePass");
    }
        
    if (FAILED(g_pBlinnPhongEffect->SetTechnique(hTechnique)))
        return;
    
    g_pDevice->SetVertexDeclaration(g_pRoomVertexDecl);
    g_pDevice->SetStreamSource(0, g_pRoomVertexBuffer, 0, sizeof(Vertex));

    if (g_disableColorMapTexture)
        g_pBlinnPhongEffect->SetTexture("colorMapTexture", g_pNullTexture);

    g_pBlinnPhongEffect->SetValue("material.ambient", g_dullMaterial.ambient, sizeof(g_dullMaterial.ambient));
    g_pBlinnPhongEffect->SetValue("material.diffuse", g_dullMaterial.diffuse, sizeof(g_dullMaterial.diffuse));
    g_pBlinnPhongEffect->SetValue("material.emissive", g_dullMaterial.emissive, sizeof(g_dullMaterial.emissive));
    g_pBlinnPhongEffect->SetValue("material.specular", g_dullMaterial.specular, sizeof(g_dullMaterial.specular));
    g_pBlinnPhongEffect->SetFloat("material.shininess", g_dullMaterial.shininess);

    // Draw walls.

    if (!g_disableColorMapTexture)
        g_pBlinnPhongEffect->SetTexture("colorMapTexture", g_pWallColorTexture);

    if (SUCCEEDED(g_pBlinnPhongEffect->Begin(&totalPasses, 0)))
    {
        for (UINT pass = 0; pass < totalPasses; ++pass)
        {
            if (SUCCEEDED(g_pBlinnPhongEffect->BeginPass(pass)))
            {
                g_pDevice->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 8);
                g_pBlinnPhongEffect->EndPass();
            }
        }

        g_pBlinnPhongEffect->End();
    }

    g_pBlinnPhongEffect->SetValue("material.ambient", g_shinyMaterial.ambient, sizeof(g_shinyMaterial.ambient));
    g_pBlinnPhongEffect->SetValue("material.diffuse", g_shinyMaterial.diffuse, sizeof(g_shinyMaterial.diffuse));
    g_pBlinnPhongEffect->SetValue("material.emissive", g_shinyMaterial.emissive, sizeof(g_shinyMaterial.emissive));
    g_pBlinnPhongEffect->SetValue("material.specular", g_shinyMaterial.specular, sizeof(g_shinyMaterial.specular));
    g_pBlinnPhongEffect->SetFloat("material.shininess", g_shinyMaterial.shininess);

    // Draw ceiling.

    if (!g_disableColorMapTexture)
        g_pBlinnPhongEffect->SetTexture("colorMapTexture", g_pCeilingColorTexture);

    if (SUCCEEDED(g_pBlinnPhongEffect->Begin(&totalPasses, 0)))
    {
        for (UINT pass = 0; pass < totalPasses; ++pass)
        {
            if (SUCCEEDED(g_pBlinnPhongEffect->BeginPass(pass)))
            {
                g_pDevice->DrawPrimitive(D3DPT_TRIANGLELIST, 24, 2);
                g_pBlinnPhongEffect->EndPass();
            }
        }

        g_pBlinnPhongEffect->End();
    }

    // Draw floor.

    if (!g_disableColorMapTexture)
        g_pBlinnPhongEffect->SetTexture("colorMapTexture", g_pFloorColorTexture);

    if (SUCCEEDED(g_pBlinnPhongEffect->Begin(&totalPasses, 0)))
    {
        for (UINT pass = 0; pass < totalPasses; ++pass)
        {
            if (SUCCEEDED(g_pBlinnPhongEffect->BeginPass(pass)))
            {
                g_pDevice->DrawPrimitive(D3DPT_TRIANGLELIST, 30, 2);
                g_pBlinnPhongEffect->EndPass();
            }
        }

        g_pBlinnPhongEffect->End();
    }
}

void RenderText()
{
    static RECT rcClient;

    std::ostringstream output;

    if (g_displayHelp)
    {
        output
            << "Left mouse click and drag to track camera" << std::endl
            << "Middle mouse click and drag to dolly camera" << std::endl
            << "Right mouse click and drag to orbit camera" << std::endl
            << "Mouse wheel to dolly camera" << std::endl
            << std::endl
            << "Press +/- to increase/decrease light radius" << std::endl
            << "Press SPACE to start/stop light animation" << std::endl
            << "Press L to enable/disable rendering of lights" << std::endl
            << "Press M to enable/disable multi pass lighting [Shader Model 2.0]" << std::endl
            << "Press S to toggle between Shader Model 2.0 and 3.0" << std::endl
            << "Press T to enable/disable textures" << std::endl
            << "Press ALT + ENTER to toggle full screen" << std::endl
            << "Press ESC to exit" << std::endl
            << std::endl
            << "Press H to hide help";
    }
    else
    {
        output << "FPS: " << g_framesPerSecond << std::endl;

        if (g_msaaSamples > 1)
            output << "Multisample anti-aliasing: " << g_msaaSamples << "x" << std::endl;

        output << "Anisotropic filtering: " << g_maxAnisotrophy << "x" << std::endl;

        if (g_pBlinnPhongEffect == g_pBlinnPhongEffectSM30)
        {
            output
                << "Shader Model 3.0" << std::endl
                << "Technique: Single pass lighting" << std::endl;
        }
        else
        {
            output << "Shader Model 2.0" << std::endl;

            if (g_enableMultipassLighting)
                output << "Technique: Multi pass lighting" << std::endl;
            else
                output << "Technique: Single pass lighting" << std::endl;
        }

        output
            << "Light radius: " << g_lights[0].radius << std::endl
            << std::endl
            << "Press H to display help";
    }

    GetClientRect(g_hWnd, &rcClient);
    rcClient.left += 4;
    rcClient.top += 2;

    g_pFont->DrawText(0, output.str().c_str(), -1, &rcClient,
        DT_EXPANDTABS | DT_LEFT, D3DCOLOR_XRGB(255, 255, 0));
}

bool ResetDevice()
{
    if (FAILED(g_pBlinnPhongEffectSM20->OnLostDevice()))
        return false;

    if (FAILED(g_pBlinnPhongEffectSM30->OnLostDevice()))
        return false;

    if (FAILED(g_pAmbientEffect->OnLostDevice()))
        return false;

    if (FAILED(g_pFont->OnLostDevice()))
        return false;

    if (FAILED(g_pDevice->Reset(&g_params)))
        return false;

    if (FAILED(g_pFont->OnResetDevice()))
        return false;

    if (FAILED(g_pAmbientEffect->OnResetDevice()))
        return false;

    if (FAILED(g_pBlinnPhongEffectSM20->OnResetDevice()))
        return false;

    if (FAILED(g_pBlinnPhongEffectSM30->OnResetDevice()))
        return false;

    return true;
}

void SetProcessorAffinity()
{
    // Assign the current thread to one processor. This ensures that timing
    // code runs on only one processor, and will not suffer any ill effects
    // from power management.
    //
    // Based on the DXUTSetProcessorAffinity() function in the DXUT framework.

    DWORD_PTR dwProcessAffinityMask = 0;
    DWORD_PTR dwSystemAffinityMask = 0;
    HANDLE hCurrentProcess = GetCurrentProcess();

    if (!GetProcessAffinityMask(hCurrentProcess, &dwProcessAffinityMask, &dwSystemAffinityMask))
        return;

    if (dwProcessAffinityMask)
    {
        // Find the lowest processor that our process is allowed to run against.

        DWORD_PTR dwAffinityMask = (dwProcessAffinityMask & ((~dwProcessAffinityMask) + 1));

        // Set this as the processor that our thread must always run against.
        // This must be a subset of the process affinity mask.

        HANDLE hCurrentThread = GetCurrentThread();

        if (hCurrentThread != INVALID_HANDLE_VALUE)
        {
            SetThreadAffinityMask(hCurrentThread, dwAffinityMask);
            CloseHandle(hCurrentThread);
        }
    }

    CloseHandle(hCurrentProcess);
}

void ToggleFullScreen()
{
    static DWORD savedExStyle;
    static DWORD savedStyle;
    static RECT rcSaved;

    g_isFullScreen = !g_isFullScreen;

    if (g_isFullScreen)
    {
        // Moving to full screen mode.

        savedExStyle = GetWindowLong(g_hWnd, GWL_EXSTYLE);
        savedStyle = GetWindowLong(g_hWnd, GWL_STYLE);
        GetWindowRect(g_hWnd, &rcSaved);

        SetWindowLong(g_hWnd, GWL_EXSTYLE, 0);
        SetWindowLong(g_hWnd, GWL_STYLE, WS_POPUP);
        SetWindowPos(g_hWnd, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);

        g_windowWidth = GetSystemMetrics(SM_CXSCREEN);
        g_windowHeight = GetSystemMetrics(SM_CYSCREEN);

        SetWindowPos(g_hWnd, HWND_TOPMOST, 0, 0,
            g_windowWidth, g_windowHeight, SWP_SHOWWINDOW);

        // Update presentation parameters.

        g_params.Windowed = FALSE;
        g_params.BackBufferWidth = g_windowWidth;
        g_params.BackBufferHeight = g_windowHeight;

        if (g_enableVerticalSync)
        {
            g_params.FullScreen_RefreshRateInHz = D3DPRESENT_INTERVAL_DEFAULT;
            g_params.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
        }
        else
        {
            g_params.FullScreen_RefreshRateInHz = D3DPRESENT_INTERVAL_IMMEDIATE;
            g_params.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        }
    }
    else
    {
        // Moving back to windowed mode.

        SetWindowLong(g_hWnd, GWL_EXSTYLE, savedExStyle);
        SetWindowLong(g_hWnd, GWL_STYLE, savedStyle);
        SetWindowPos(g_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);

        g_windowWidth = rcSaved.right - rcSaved.left;
        g_windowHeight = rcSaved.bottom - rcSaved.top;

        SetWindowPos(g_hWnd, HWND_NOTOPMOST, rcSaved.left, rcSaved.top,
            g_windowWidth, g_windowHeight, SWP_SHOWWINDOW);

        // Update presentation parameters.

        g_params.Windowed = TRUE;
        g_params.BackBufferWidth = g_windowWidth;
        g_params.BackBufferHeight = g_windowHeight;
        g_params.FullScreen_RefreshRateInHz = 0;

        if (g_enableVerticalSync)
            g_params.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
        else
            g_params.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    }

    ResetDevice();
}

void UpdateFrame(float elapsedTimeSec)
{
    UpdateFrameRate(elapsedTimeSec);
    
    if (g_animateLights)
        UpdateLights(elapsedTimeSec);
    
    UpdateEffects();
}

void UpdateFrameRate(float elapsedTimeSec)
{
    static float accumTimeSec = 0.0f;
    static int frames = 0;

    accumTimeSec += elapsedTimeSec;

    if (accumTimeSec > 1.0f)
    {
        g_framesPerSecond = frames;

        frames = 0;
        accumTimeSec = 0.0f;
    }
    else
    {
        ++frames;
    }
}

void UpdateEffects()
{
    static const D3DXMATRIX identity(1.0f, 0.0f, 0.0f, 0.0f,
                                     0.0f, 1.0f, 0.0f, 0.0f,
                                     0.0f, 0.0f, 1.0f, 0.0f,
                                     0.0f, 0.0f, 0.0f, 1.0f);
    static D3DXMATRIX view, proj;
    static D3DXMATRIX rot, xRot, yRot;

    // Build the perspective projection matrix.

    D3DXMatrixPerspectiveFovLH(&proj, CAMERA_FOVY,
        static_cast<float>(g_windowWidth) / static_cast<float>(g_windowHeight),
        CAMERA_ZNEAR, CAMERA_ZFAR);

    // Build the view matrix.

    D3DXQuaternionNormalize(&g_camera.orientation, &g_camera.orientation);
    D3DXMatrixRotationQuaternion(&view, &g_camera.orientation);
    
    g_camera.xAxis = D3DXVECTOR3(view(0,0), view(1,0), view(2,0));
    g_camera.yAxis = D3DXVECTOR3(view(0,1), view(1,1), view(2,1));
    g_camera.zAxis = D3DXVECTOR3(view(0,2), view(1,2), view(2,2));
   
    g_camera.pos = g_camera.target + g_camera.zAxis * -g_camera.offset;

    view(3,0) = -D3DXVec3Dot(&g_camera.xAxis, &g_camera.pos);
    view(3,1) = -D3DXVec3Dot(&g_camera.yAxis, &g_camera.pos);
    view(3,2) = -D3DXVec3Dot(&g_camera.zAxis, &g_camera.pos);
    
    g_camera.viewProjectionMatrix = view * proj;

    ID3DXEffect *pEffect = g_pBlinnPhongEffect;

    // Set the matrices for the shader.

    pEffect->SetMatrix("worldMatrix", &identity);
    pEffect->SetMatrix("worldInverseTransposeMatrix", &identity);
    pEffect->SetMatrix("worldViewProjectionMatrix", &g_camera.viewProjectionMatrix);

    // Set the camera position.
    
    pEffect->SetValue("cameraPos", &g_camera.pos, sizeof(g_camera.pos));

    // Set the scene global ambient term.
    
    pEffect->SetValue("globalAmbient", &g_sceneAmbient, sizeof(g_sceneAmbient));

    // Set the number of active lights. For shader model 3.0 only.
    
    if (pEffect == g_pBlinnPhongEffectSM30)
        pEffect->SetValue("numLights", &g_numLights, sizeof(g_numLights));

    // Set the lighting parameters for the shader.
    
    const PointLight *pLight = 0;
    D3DXHANDLE hLight;
    D3DXHANDLE hLightPos;
    D3DXHANDLE hLightAmbient;
    D3DXHANDLE hLightDiffuse;
    D3DXHANDLE hLightSpecular;
    D3DXHANDLE hLightRadius;

    for (int i = 0; i < g_numLights; ++i)
    {
        pLight = &g_lights[i];
        hLight = pEffect->GetParameterElement("lights", i);
        
        hLightPos = pEffect->GetParameterByName(hLight, "pos");
        hLightAmbient = pEffect->GetParameterByName(hLight, "ambient");
        hLightDiffuse = pEffect->GetParameterByName(hLight, "diffuse");
        hLightSpecular = pEffect->GetParameterByName(hLight, "specular");
        hLightRadius = pEffect->GetParameterByName(hLight, "radius");

        pEffect->SetValue(hLightPos, pLight->pos, sizeof(pLight->pos));
        pEffect->SetValue(hLightAmbient, pLight->ambient, sizeof(pLight->ambient));
        pEffect->SetValue(hLightDiffuse, pLight->diffuse, sizeof(pLight->diffuse));
        pEffect->SetValue(hLightSpecular, pLight->specular, sizeof(pLight->specular));
        pEffect->SetFloat(hLightRadius, pLight->radius);
    }
}

void UpdateLights(float elapsedTimeSec)
{
    for (int i = 0; i < sizeof(g_lights) / sizeof(g_lights[0]); ++i)
        g_lights[i].update(elapsedTimeSec);   
}