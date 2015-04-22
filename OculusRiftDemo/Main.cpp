#include <iostream>
#include <cassert>
#include <SDL.h>
#include <SDL_syswm.h>
#include <GL/glew.h>
#include <Kernel/OVR_System.h>
#include <OVR_Kernel.h>
#include <OVR_CAPI_GL.h>
using namespace OVR;

//Global variables
SDL_Window* window;
SDL_GLContext context;
ovrHmd hmd;
ovrEyeRenderDesc eyeRenderDesc[2];
struct RenderTarget
{
    ovrSizei size;
    GLuint tex;
    GLuint depthTex;
    GLuint fbo;
} targets[2];

void init();
void end();
void update();
void setupRenderTarget(const RenderTarget& target);
void drawScene(Matrix4f view, Matrix4f proj);
void drawCube(float size);



int main(int argc, char* argv[])
{
    init();

    bool running = true;
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EventType::SDL_QUIT)
            {
                running = false;
            }
        }

        update();
    }

    end();
    return 0;
}

void init()
{
    //SDL Initialization
    SDL_Init(SDL_INIT_EVERYTHING);

    //Request OpenGL 3.0 compatibility profile
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    //Create window
    uint32_t flags = SDL_WINDOW_OPENGL;
    //if (!windowed) { flags |= SDL_WINDOW_FULLSCREEN; }
    window = SDL_CreateWindow("Oculus Rift Demo",
        0, 0, 1920, 1080, flags);
    context = SDL_GL_CreateContext(window);

    //Glew
    glewInit();


    //Oculus Initialization
    OVR::System::Init(OVR::Log::ConfigureDefaultLog(OVR::LogMask_All));
    ovr_Initialize();
    hmd = ovrHmd_Create(0);
    if (hmd == nullptr)
    {
        hmd = ovrHmd_CreateDebug(ovrHmd_DK2);
    }
    assert(hmd != nullptr);

    //bool windowed = (hmd->HmdCaps & ovrHmdCap_ExtendDesktop) ? false : true;

    //Get the Win32 window handle (HWND) and device context (HDC)
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(window, &wmInfo);

    ovrGLConfig config;
    config.OGL.Header.API = ovrRenderAPI_OpenGL;
    config.OGL.Header.BackBufferSize = hmd->Resolution;
    config.OGL.Header.Multisample = 0;
    config.OGL.Window = wmInfo.info.win.window;
    config.OGL.DC = GetDC(config.OGL.Window);

    ovrHmd_ConfigureRendering(hmd, &config.Config,
        ovrDistortionCap_Vignette | ovrDistortionCap_TimeWarp | ovrDistortionCap_Overdrive,
        hmd->DefaultEyeFov, eyeRenderDesc);
    ovrHmd_SetEnabledCaps(hmd, ovrHmdCap_LowPersistence | ovrHmdCap_DynamicPrediction);
    ovrHmd_AttachToWindow(hmd, wmInfo.info.win.window, nullptr, nullptr);

    ovrHmd_ConfigureTracking(hmd, ovrTrackingCap_Orientation |
        ovrTrackingCap_MagYawCorrection | ovrTrackingCap_Position, 0);
    ovrHmd_DismissHSWDisplay(hmd);

    //Set up render targets
    for (int i = 0; i < 2; i++)
    {
        ovrSizei texSize = ovrHmd_GetFovTextureSize(hmd, (ovrEyeType)i, hmd->DefaultEyeFov[i], 1);
        targets[i].size = texSize;

        glGenTextures(1, &targets[i].tex);
        glBindTexture(GL_TEXTURE_2D, targets[i].tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
            texSize.w, texSize.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glGenTextures(1, &targets[i].depthTex);
        glBindTexture(GL_TEXTURE_2D, targets[i].depthTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
            texSize.w, texSize.h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);

        glCreateFramebuffers(1, &targets[i].fbo);
    }
}

void end()
{
    //SDL cleanup stuff
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

void update()
{
    ovrHmd_BeginFrame(hmd, 0);

    Vector3f position(0, ovrHmd_GetFloat(hmd, OVR_KEY_EYE_HEIGHT, 1.6f), 0);
    ovrVector3f viewOffset[2] = { eyeRenderDesc[0].HmdToEyeViewOffset, eyeRenderDesc[1].HmdToEyeViewOffset };
    ovrPosef eyeRenderPose[2];
    ovrHmd_GetEyePoses(hmd, 0, viewOffset, eyeRenderPose, NULL);
    for (int eye = 0; eye < 2; eye++)
    {
        Matrix4f rollPitchYaw = Matrix4f::RotationY(0);
        Matrix4f finalRollPitchYaw = rollPitchYaw * Matrix4f(eyeRenderPose[eye].Orientation);
        Vector3f finalUp = finalRollPitchYaw.Transform(Vector3f(0, 1, 0));
        Vector3f finalForward = finalRollPitchYaw.Transform(Vector3f(0, 0, -1));
        Vector3f shiftedEyePos = position + rollPitchYaw.Transform(eyeRenderPose[eye].Position);

        Matrix4f view = Matrix4f::LookAtRH(shiftedEyePos, shiftedEyePos + finalForward, finalUp);
        Matrix4f proj = ovrMatrix4f_Projection(hmd->DefaultEyeFov[eye], 0.2f, 1000.0f, ovrProjection_RightHanded);

        setupRenderTarget(targets[eye]);
        drawScene(view, proj);
    }

    //Do distortion rendering
    ovrGLTexture eyeTex[2];
    for (int i = 0; i < 2; i++)
    {
        eyeTex[i].OGL.Header.API = ovrRenderAPI_OpenGL;
        eyeTex[i].OGL.Header.TextureSize = targets[i].size;
        eyeTex[i].OGL.Header.RenderViewport = Recti(Vector2i(0, 0), targets[i].size);
        eyeTex[i].OGL.TexId = targets[i].tex;
    }

    ovrHmd_EndFrame(hmd, eyeRenderPose, &eyeTex[0].Texture);
    //SDL_GL_SwapWindow(window);
}

void setupRenderTarget(const RenderTarget& target)
{
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.tex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, target.depthTex, 0);

    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, target.size.w, target.size.h);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void drawScene(Matrix4f view, Matrix4f proj)
{
    glClearColor(0.2f, 0.2f, 0.2f, 1);
    
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glLoadTransposeMatrixf(&proj.M[0][0]);  //OVR matrices need to be transposed for OpenGL

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glLoadTransposeMatrixf(&view.M[0][0]);

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    float diffuse[] = { 1, 1, 0.8f, 1 };
    float ambient[] = { 0.15f, 0.15f, 0.25f, 1 };
    float position[] = { 0.1, 0.4, 1, 0 };
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
    glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
    glLightfv(GL_LIGHT0, GL_POSITION, position);

    for (int i = -10; i < 10; i++)
    {
        for (int j = -10; j < 10; j++)
        {
            glPushMatrix();
            glTranslatef(i * 4, i * j, j * 4);
            drawCube(1);
            glPopMatrix();
        }
    }

    for (int i = 0; i < 12; i++)
    {
        glPushMatrix();
        glRotatef(ovr_GetTimeInSeconds() * 10 + i * 30, 0, 1, 0);
        glTranslatef(0, 0, -10);
        drawCube(0.5f);
        glPopMatrix();
    }
}

void drawCube(float size)
{
    glBegin(GL_QUADS);
        glColor3f(0.8f, 0.8f, 0.8f);

        //Top
        glNormal3f(0, 1, 0);
        glVertex3f(-size, size, -size);
        glVertex3f(-size, size, size);
        glVertex3f(size, size, size);
        glVertex3f(size, size, -size);

        //Bottom
        glNormal3f(0, -1, 0);
        glVertex3f(-size, -size, -size);
        glVertex3f(-size, -size, size);
        glVertex3f(size, -size, size);
        glVertex3f(size, -size, -size);

        //Front
        glNormal3f(0, 0, 1);
        glVertex3f(-size, -size, size);
        glVertex3f(-size, size, size);
        glVertex3f(size, size, size);
        glVertex3f(size, -size, size);

        //Back
        glNormal3f(0, 0, -1);
        glVertex3f(-size, -size, -size);
        glVertex3f(-size, size, -size);
        glVertex3f(size, size, -size);
        glVertex3f(size, -size, -size);

        //Left
        glNormal3f(1, 0, 0);
        glVertex3f(size, -size, -size);
        glVertex3f(size, -size, size);
        glVertex3f(size, size, size);
        glVertex3f(size, size, -size);

        //Right
        glNormal3f(-1, 0, 0);
        glVertex3f(-size, -size, -size);
        glVertex3f(-size, -size, size);
        glVertex3f(-size, size, size);
        glVertex3f(-size, size, -size);
    glEnd();
}
