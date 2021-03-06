#include "AliHLTTPCCADef.h"
#include "opengl_backend.h"

#include <vector>
#include <array>
#include <tuple>
#include <memory>

#ifndef WIN32
#include "bitmapfile.h"
#endif
#ifdef HLTCA_HAVE_OPENMP
#include <omp.h>
#endif

#include "AliHLTTPCCASliceData.h"
#include "AliHLTTPCCAStandaloneFramework.h"
#include "AliHLTTPCCATrack.h"
#include "AliHLTTPCCATracker.h"
#include "AliHLTTPCCATrackerFramework.h"
#include "AliHLTTPCGMMergedTrack.h"
#include "AliHLTTPCGMPropagator.h"
#include "include.h"
#include "../cmodules/timer.h"
#include "../cmodules/qconfig.h"

//#define CHKERR(cmd) {cmd;}
#define CHKERR(cmd) {(cmd); GLenum err = glGetError(); while (err != GL_NO_ERROR) {printf("OpenGL Error %d: %s (%s: %d)\n", err, gluErrorString(err), __FILE__, __LINE__);exit(1);}}

#define OPENGL_EMULATE_MULTI_DRAW 0

#define fgkNSlices 36
volatile int needUpdate = 0;
void ShowNextEvent() {needUpdate = 1;}
#define GL_SCALE_FACTOR 100.f

#define TRACK_TYPE_ID_LIMIT 100
#define SEPERATE_GLOBAL_TRACKS_LIMIT (separateGlobalTracks ? 6 : TRACK_TYPE_ID_LIMIT)

OpenGLConfig cfg;
static auto& config = configStandalone.configGL;
AliHLTTPCCAStandaloneFramework &hlt = AliHLTTPCCAStandaloneFramework::Instance();
const AliHLTTPCGMMerger &merger = hlt.Merger();

struct DrawArraysIndirectCommand
{
	DrawArraysIndirectCommand(uint a = 0, uint b = 0, uint c = 0, uint d = 0) : count(a), instanceCount(b), first(c), baseInstance(d) {}
	uint  count;
	uint  instanceCount;
	uint  first;
	uint  baseInstance;
};
GLuint vbo_id[fgkNSlices], indirect_id;
int indirectSliceOffset[fgkNSlices];
typedef std::tuple<GLsizei, GLsizei, int> vboList;
struct GLvertex {GLfloat x, y, z; GLvertex(GLfloat a, GLfloat b, GLfloat c) : x(a), y(b), z(c) {}};
vecpod<GLvertex> vertexBuffer[fgkNSlices];
vecpod<GLint> vertexBufferStart[fgkNSlices];
vecpod<GLsizei> vertexBufferCount[fgkNSlices];
int drawCalls = 0;
bool useGLIndirectDraw = true;
bool useMultiVBO = false;
inline void drawVertices(const vboList& v, const GLenum t)
{
	auto first = std::get<0>(v);
	auto count = std::get<1>(v);
	auto iSlice = std::get<2>(v);
	if (count == 0) return;
	drawCalls += count;

	if (useMultiVBO)
	{
		CHKERR(glBindBuffer(GL_ARRAY_BUFFER, vbo_id[iSlice]))
		CHKERR(glVertexPointer(3, GL_FLOAT, 0, 0));
	}

	if (useGLIndirectDraw)
	{
		CHKERR(glMultiDrawArraysIndirect(t, (void*) (size_t) ((indirectSliceOffset[iSlice] + first) * sizeof(DrawArraysIndirectCommand)), count, 0));
	}
	else if (OPENGL_EMULATE_MULTI_DRAW)
	{
		for (int k = 0;k < count;k++) CHKERR(glDrawArrays(t, vertexBufferStart[iSlice][first + k], vertexBufferCount[iSlice][first + k]));
	}
	else
	{
		CHKERR(glMultiDrawArrays(t, vertexBufferStart[iSlice].data() + first, vertexBufferCount[iSlice].data() + first, count));
	}
}
inline void insertVertexList(std::pair<vecpod<GLint>*, vecpod<GLsizei>*>& vBuf, size_t first, size_t last)
{
	if (first == last) return;
	vBuf.first->emplace_back(first);
	vBuf.second->emplace_back(last - first);
}
inline void insertVertexList(int iSlice, size_t first, size_t last)
{
	std::pair<vecpod<GLint>*, vecpod<GLsizei>*> vBuf(vertexBufferStart + iSlice, vertexBufferCount + iSlice);
	insertVertexList(vBuf, first, last);
}

bool invertColors = false;
const int drawQualityPoint = 0;
const int drawQualityLine = 0;
const int drawQualityPerspective = 0;
const int drawQualityRenderToTexture = 1;
int drawQualityMSAA = 0;
int drawQualityDownsampleFSAA = 0;
bool drawQualityVSync = 0;
int maxFPSRate = 0;

int testSetting = 0;

bool camLookOrigin = false;
bool camYUp = false;
int cameraMode = 0;

float angleRollOrigin = -1e9;
float maxClusterZ = 0;

int screenshot_scale = 1;

const int init_width = 1024, init_height = 768;
int screen_width = init_width, screen_height = init_height;
int render_width = init_width, render_height = init_height;

bool separateGlobalTracks = 0;
bool propagateLoopers = 0;

float mouseDnX, mouseDnY;
float mouseMvX, mouseMvY;
bool mouseDn = false;
bool mouseDnR = false;
int mouseWheel = 0;
bool keys[256] = {false}; // Array Used For The Keyboard Routine
bool keysShift[256] = {false}; //Shift held when key down

volatile int exitButton = 0;
volatile int sendKey = 0;

GLfloat currentMatrix[16];
float xyz[3];
float angle[3];
float rphitheta[3];
float quat[4];
template <typename... Args> void SetInfo(Args... args);
void calcXYZ()
{
	xyz[0] = -(currentMatrix[0] * currentMatrix[12] + currentMatrix[1] * currentMatrix[13] + currentMatrix[2] * currentMatrix[14]);
	xyz[1] = -(currentMatrix[4] * currentMatrix[12] + currentMatrix[5] * currentMatrix[13] + currentMatrix[6] * currentMatrix[14]);
	xyz[2] = -(currentMatrix[8] * currentMatrix[12] + currentMatrix[9] * currentMatrix[13] + currentMatrix[10] * currentMatrix[14]);

	angle[0] = -asinf(currentMatrix[6]); //Invert rotY*rotX*rotZ
	float A = cosf(angle[0]);
	if (fabs(A) > 0.005)
	{
		angle[1] = atan2f(-currentMatrix[2] / A, currentMatrix[10] / A);
		angle[2] = atan2f(currentMatrix[4] / A, currentMatrix[5] / A);
	}
	else
	{
		angle[1] = 0;
		angle[2] = atan2f(-currentMatrix[1], -currentMatrix[0]);
	}

	rphitheta[0] = sqrtf(xyz[0] * xyz[0] + xyz[1] * xyz[1] + xyz[2] * xyz[2]);
	rphitheta[1] = atan2f(xyz[0], xyz[2]);
	rphitheta[2] = atan2f(xyz[1], sqrtf(xyz[0] * xyz[0] + xyz[2] * xyz[2]));

	createQuaternionFromMatrix(quat, currentMatrix);

	/*float angle[1] = -asinf(currentMatrix[2]); //Calculate Y-axis angle - for rotX*rotY*rotZ
	float C = cosf( angle_y );
	if (fabs(C) > 0.005) //Gimball lock?
	{
		angle[0]  = atan2f(-currentMatrix[6] / C, currentMatrix[10] / C);
		angle[2]  = atan2f(-currentMatrix[1] / C, currentMatrix[0] / C);
	}
	else
	{
		angle[0]  = 0; //set x-angle
		angle[2]  = atan2f(currentMatrix[4], currentMatrix[5]);
	}*/
}

int projectxy = 0;

int markClusters = 0;
int hideRejectedClusters = 1;
int hideUnmatchedClusters = 0;
int hideRejectedTracks = 1;
int markAdjacentClusters = 0;

vecpod<std::array<int,37>> collisionClusters;
int nCollisions = 1;
void SetCollisionFirstCluster(unsigned int collision, int slice, int cluster)
{
	nCollisions = collision + 1;
	collisionClusters.resize(nCollisions);
	collisionClusters[collision][slice] = cluster;
}

float Xadd = 0;
float Zadd = 0;

std::unique_ptr<float4[]> globalPosPtr = nullptr;
float4* globalPos;
int maxClusters = 0;
int currentClusters = 0;

volatile int displayEventNr = 0;
int currentEventNr = -1;

int glDLrecent = 0;
int updateDLList = 0;

int animate = 0;
HighResTimer animationTimer;
int animationFrame = 0;
int animationLastBase = 0;
int animateScreenshot = 0;
int animationExport = 0;
bool animationChangeConfig = true;
float animationDelay = 2.f;
vecpod<float> animateVectors[9];
vecpod<OpenGLConfig> animateConfig;
opengl_spline animationSplines[8];
void animationCloseAngle(float& newangle, float lastAngle)
{
	const float delta = lastAngle > newangle ? (2 * M_PI) : (-2 * M_PI);
	while (fabs(newangle + delta - lastAngle) < fabs(newangle - lastAngle)) newangle += delta;
}
void animateCloseQuaternion(float* v, float lastx, float lasty, float lastz, float lastw)
{
	float distPos2 = (lastx - v[0]) * (lastx - v[0]) + (lasty - v[1]) * (lasty - v[1]) + (lastz - v[2]) * (lastz - v[2]) + (lastw - v[3]) * (lastw - v[3]);
	float distNeg2 = (lastx + v[0]) * (lastx + v[0]) + (lasty + v[1]) * (lasty + v[1]) + (lastz + v[2]) * (lastz + v[2]) + (lastw + v[3]) * (lastw + v[3]);
	if (distPos2 > distNeg2)
	{
		for (int i = 0;i < 4;i++) v[i] = -v[i];
	}
}
void setAnimationPoint()
{
	if (cfg.animationMode & 4) //Spherical
	{
		float rxy = sqrtf(xyz[0] * xyz[0] + xyz[2] * xyz[2]);
		float anglePhi = atan2f(xyz[0], xyz[2]);
		float angleTheta = atan2f(xyz[1], rxy);
		if (animateVectors[0].size()) animationCloseAngle(anglePhi, animateVectors[2].back());
		if (animateVectors[0].size()) animationCloseAngle(angleTheta, animateVectors[3].back());
		animateVectors[1].emplace_back(0);
		animateVectors[2].emplace_back(anglePhi);
		animateVectors[3].emplace_back(angleTheta);
	}
	else
	{
		for (int i = 0;i < 3;i++) {animateVectors[i + 1].emplace_back(xyz[i]);} //Cartesian
	}
	float r = sqrtf(xyz[0] * xyz[0] + xyz[1] * xyz[1] + xyz[2] * xyz[2]);
	animateVectors[4].emplace_back(r);
	if (cfg.animationMode & 1) //Euler-angles
	{
		for (int i = 0;i < 3;i++)
		{
			float newangle = angle[i];
			if (animateVectors[0].size()) animationCloseAngle(newangle, animateVectors[i + 5].back());
			animateVectors[i + 5].emplace_back(newangle);
		}
		animateVectors[8].emplace_back(0);
	}
	else //Quaternions
	{
		float v[4];
		createQuaternionFromMatrix(v, currentMatrix);
		if (animateVectors[0].size()) animateCloseQuaternion(v, animateVectors[5].back(), animateVectors[6].back(), animateVectors[7].back(), animateVectors[8].back());
		for (int i = 0;i < 4;i++) animateVectors[i + 5].emplace_back(v[i]);
	}
	float delay = 0.f;
	if (animateVectors[0].size()) delay = animateVectors[0].back() + ((int) (animationDelay * 20)) / 20.f;;
	animateVectors[0].emplace_back(delay);
	animateConfig.emplace_back(cfg);
}
void resetAnimation()
{
	for (int i = 0;i < 9;i++) animateVectors[i].clear();
	animateConfig.clear();
	animate = 0;
}
void removeAnimationPoint()
{
	if (animateVectors[0].size() == 0) return;
	for (int i = 0;i < 9;i++) animateVectors[i].pop_back();
	animateConfig.pop_back();
}
void startAnimation()
{
	for (int i = 0;i < 8;i++) animationSplines[i].create(animateVectors[0], animateVectors[i + 1]);
	animationTimer.ResetStart();
	animationFrame = 0;
	animate = 1;
	animationLastBase = 0;
}

volatile int resetScene = 0;

int printInfoText = 1;
char infoText2[1024];
HighResTimer infoText2Timer, infoHelpTimer;
void showInfo(const char* info);
template <typename... Args> void SetInfo(Args... args)
{
	sprintf(infoText2, args...);
	infoText2Timer.ResetStart();
}

static constexpr const int N_POINTS_TYPE = 9;
static constexpr const int N_LINES_TYPE = 6;
static constexpr const int N_FINAL_TYPE = 4;
inline void SetColorClusters() { if (cfg.colorCollisions) return; if (invertColors) glColor3f(0, 0.3, 0.7); else glColor3f(0, 0.7, 1.0); }
inline void SetColorInitLinks() { if (invertColors) glColor3f(0.42, 0.4, 0.1); else glColor3f(0.42, 0.4, 0.1); }
inline void SetColorLinks() { if (invertColors) glColor3f(0.6, 0.1, 0.1); else glColor3f(0.8, 0.2, 0.2); }
inline void SetColorSeeds() { if (invertColors) glColor3f(0.6, 0.0, 0.65); else glColor3f(0.8, 0.1, 0.85); }
inline void SetColorTracklets() { if (invertColors) glColor3f(0, 0, 0); else glColor3f(1, 1, 1); }
inline void SetColorTracks() { if (invertColors) glColor3f(0.6, 0, 0.1); else glColor3f(0.8, 1., 0.15); }
inline void SetColorGlobalTracks() { if (invertColors) glColor3f(0.8, 0.2, 0); else glColor3f(1.0, 0.4, 0); }
inline void SetColorFinal() { if (cfg.colorCollisions) return; if (invertColors) glColor3f(0, 0.6, 0.1); else glColor3f(0, 0.7, 0.2); }
inline void SetColorGrid() { if (invertColors) glColor3f(0.5, 0.5, 0); else glColor3f(0.7, 0.7, 0.0); }
inline void SetColorMarked() { if (invertColors) glColor3f(0.8, 0, 0); else glColor3f(1.0, 0.0, 0.0); }
inline void SetCollisionColor(int col)
{
	int red = (col * 2) % 5;
	int blue =  (2 + col * 3) % 7;
	int green = (4 + col * 5) % 6;
	if (invertColors && red == 4 && blue == 5 && green == 6) red = 0;
	if (!invertColors && red == 0 && blue == 0 && green == 0) red = 4;
	glColor3f(red / 4., green / 5., blue / 6.);
}

void setQuality()
{
	//Doesn't seem to make a difference in this applicattion
	CHKERR(glHint(GL_POINT_SMOOTH_HINT, drawQualityPoint == 2 ? GL_NICEST : drawQualityPoint == 1 ? GL_DONT_CARE : GL_FASTEST));
	CHKERR(glHint(GL_LINE_SMOOTH_HINT, drawQualityLine == 2 ? GL_NICEST : drawQualityLine == 1 ? GL_DONT_CARE : GL_FASTEST));
	CHKERR(glHint(GL_PERSPECTIVE_CORRECTION_HINT, drawQualityPerspective == 2 ? GL_NICEST : drawQualityPerspective == 1 ? GL_DONT_CARE : GL_FASTEST));
	if (drawQualityMSAA > 1)
	{
		CHKERR(glEnable(GL_MULTISAMPLE))
	}
	else
	{
		CHKERR(glDisable(GL_MULTISAMPLE))
	}
}

void setDepthBuffer()
{
	if (cfg.depthBuffer)
	{
		CHKERR(glEnable(GL_DEPTH_TEST));                           // Enables Depth Testing
		CHKERR(glDepthFunc(GL_LEQUAL));                            // The Type Of Depth Testing To Do
	}
	else
	{
		CHKERR(glDisable(GL_DEPTH_TEST));
	}
}

struct GLfb
{
	GLuint fb_id = 0, fbCol_id = 0, fbDepth_id = 0;
	bool tex = false;
	bool msaa = false;
	bool depth = false;
	bool created = false;
};
GLfb mixBuffer;
void createFB_texture(GLuint& id, bool msaa, GLenum storage, GLenum attachment)
{
	GLenum textureType = msaa ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
	CHKERR(glGenTextures(1, &id));
	CHKERR(glBindTexture(textureType, id));
	if (msaa)
	{
		CHKERR(glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, drawQualityMSAA, storage, render_width, render_height, false));
	}
	else
	{
		CHKERR(glTexImage2D(GL_TEXTURE_2D, 0, storage, render_width, render_height, 0, storage, GL_UNSIGNED_BYTE, NULL));
		CHKERR(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
		CHKERR(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
	}
	CHKERR(glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, textureType, id, 0));
}

void createFB_renderbuffer(GLuint& id, bool msaa, GLenum storage, GLenum attachment)
{
	CHKERR(glGenRenderbuffers(1, &id));
	CHKERR(glBindRenderbuffer(GL_RENDERBUFFER, id));
	if (msaa) CHKERR(glRenderbufferStorageMultisample(GL_RENDERBUFFER, drawQualityMSAA, storage, render_width, render_height))
	else CHKERR(glRenderbufferStorage(GL_RENDERBUFFER, storage, render_width, render_height))
	CHKERR(glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, id));
}

void createFB(GLfb& fb, bool tex, bool withDepth, bool msaa)
{
	fb.tex = tex;
	fb.depth = withDepth;
	fb.msaa = msaa;
	GLint drawFboId = 0, readFboId = 0;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFboId);
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFboId);
	CHKERR(glGenFramebuffers(1, &fb.fb_id));
	CHKERR(glBindFramebuffer(GL_FRAMEBUFFER, fb.fb_id));

	if (tex) createFB_texture(fb.fbCol_id, fb.msaa, GL_RGBA, GL_COLOR_ATTACHMENT0);
	else createFB_renderbuffer(fb.fbCol_id, fb.msaa, GL_RGBA, GL_COLOR_ATTACHMENT0);

	if (withDepth)
	{
		if (tex && fb.msaa) createFB_texture(fb.fbDepth_id, fb.msaa, GL_DEPTH_COMPONENT24, GL_DEPTH_ATTACHMENT);
		else createFB_renderbuffer(fb.fbDepth_id, fb.msaa, GL_DEPTH_COMPONENT24, GL_DEPTH_ATTACHMENT);
	}

	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		printf("Error creating framebuffer (tex %d) - incomplete (%d)\n", (int) tex, status);
		exit(1);
	}
	CHKERR(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFboId));
	CHKERR(glBindFramebuffer(GL_READ_FRAMEBUFFER, readFboId));
	fb.created = true;
}

void deleteFB(GLfb& fb)
{
	if (fb.tex) CHKERR(glDeleteTextures(1, &fb.fbCol_id))
	else CHKERR(glDeleteRenderbuffers(1, &fb.fbCol_id))
	if (fb.depth)
	{
		if (fb.tex && fb.msaa) CHKERR(glDeleteTextures(1, &fb.fbDepth_id))
		else CHKERR(glDeleteRenderbuffers(1, &fb.fbDepth_id))
	}
	CHKERR(glDeleteFramebuffers(1, &fb.fb_id));
	fb.created = false;
}

vecpod<GLuint> mainBufferStack{0};
void setFrameBuffer(int updateCurrent = -1, GLuint newID = 0)
{
	if (updateCurrent == 1) mainBufferStack.push_back(newID);
	else if (updateCurrent == 2) mainBufferStack.back() = newID;
	else if (updateCurrent == -2) newID = mainBufferStack.back();
	else if (updateCurrent == -1) {mainBufferStack.pop_back();newID = mainBufferStack.back();}
	if (newID == 0)
	{
		CHKERR(glBindFramebuffer(GL_FRAMEBUFFER, 0));
		glDrawBuffer(GL_BACK);
	}
	else
	{
		CHKERR(glBindFramebuffer(GL_FRAMEBUFFER, newID));
		GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
		glDrawBuffers(1, &drawBuffer);
	}
}

GLfb offscreenBuffer, offscreenBufferNoMSAA;
void UpdateOffscreenBuffers(bool clean = false)
{
	if (mixBuffer.created) deleteFB(mixBuffer);
	if (offscreenBuffer.created) deleteFB(offscreenBuffer);
	if (offscreenBufferNoMSAA.created) deleteFB(offscreenBufferNoMSAA);
	if (clean) return;

	if (drawQualityDownsampleFSAA > 1)
	{
		render_width = screen_width * drawQualityDownsampleFSAA;
		render_height = screen_height * drawQualityDownsampleFSAA;
	}
	else
	{
		render_width = screen_width;
		render_height = screen_height;
	}
	if (drawQualityMSAA > 1 || drawQualityDownsampleFSAA > 1)
	{
		createFB(offscreenBuffer, false, true, drawQualityMSAA > 1);
		if (drawQualityMSAA > 1 && drawQualityDownsampleFSAA > 1) createFB(offscreenBufferNoMSAA, false, true, false > 1);
	}
	createFB(mixBuffer, true, true, false);
	glViewport(0, 0, render_width, render_height); // Reset The Current Viewport
	setQuality();
}

struct threadVertexBuffer
{
	vecpod<GLvertex> buffer;
	vecpod<GLint> start[N_FINAL_TYPE];
	vecpod<GLsizei> count[N_FINAL_TYPE];
	std::pair<vecpod<GLint>*, vecpod<GLsizei>*> vBuf[N_FINAL_TYPE];
	threadVertexBuffer() : buffer()
	{
		for (int i = 0;i < N_FINAL_TYPE;i++) {vBuf[i].first = start + i; vBuf[i].second = count + i;}
	}
	void clear()
	{
		for (int i = 0;i < N_FINAL_TYPE;i++) {start[i].clear(); count[i].clear();}
	}
};
std::vector<threadVertexBuffer> threadBuffers;
std::vector<std::vector<std::array<std::array<vecpod<int>, 2>, fgkNSlices>>> threadTracks;

void ReSizeGLScene(int width, int height, bool init) // Resize And Initialize The GL Window
{
	if (height == 0) // Prevent A Divide By Zero By
	{
		height = 1; // Making Height Equal One
	}

	screen_width = width;
	screen_height = height;
	UpdateOffscreenBuffers();

	glMatrixMode(GL_PROJECTION); // Select The Projection Matrix
	glLoadIdentity();
	gluPerspective(45.0f, (GLfloat) width / (GLfloat) height, 0.1f, 1000.0f);

	glMatrixMode(GL_MODELVIEW); // Select The Modelview Matrix
	if (init)
	{
		resetScene = 1;
		glLoadIdentity();
	}
	else
	{
		glLoadMatrixf(currentMatrix);
	}

	glGetFloatv(GL_MODELVIEW_MATRIX, currentMatrix);
}

void updateConfig()
{
	setQuality();
	setDepthBuffer();
}

int InitGL()
{
	CHKERR(glewInit());

	int glVersion[2] = {0, 0};
	glGetIntegerv(GL_MAJOR_VERSION, &glVersion[0]);
	glGetIntegerv(GL_MINOR_VERSION, &glVersion[1]);
	if (glVersion[0] < 4 || (glVersion[0] == 4 && glVersion[1] < 6))
	{
		printf("Unsupported OpenGL runtime %d.%d < 4.6\n", glVersion[0], glVersion[1]);
		return(0);
	}

	CHKERR(glCreateBuffers(36, vbo_id));
	CHKERR(glBindBuffer(GL_ARRAY_BUFFER, vbo_id[0]));
	CHKERR(glGenBuffers(1, &indirect_id));
	CHKERR(glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirect_id));

	CHKERR(glShadeModel(GL_SMOOTH));                           // Enable Smooth Shading
	CHKERR(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));                      // Black Background
	setDepthBuffer();
	setQuality();
	ReSizeGLScene(init_width, init_height, true);
#ifdef HLTCA_HAVE_OPENMP
	if (configStandalone.OMPThreads != -1) omp_set_num_threads(configStandalone.OMPThreads);
	int maxThreads = omp_get_max_threads();
#else
	int maxThreads = 1;
#endif
	threadBuffers.resize(maxThreads);
	threadTracks.resize(maxThreads);
	return(1);                                     // Initialization Went OK
}

void ExitGL()
{
	UpdateOffscreenBuffers(true);
	CHKERR(glDeleteBuffers(36, vbo_id));
	CHKERR(glDeleteBuffers(1, &indirect_id));
}

inline void drawPointLinestrip(int iSlice, int cid, int id, int id_limit = TRACK_TYPE_ID_LIMIT)
{
	vertexBuffer[iSlice].emplace_back(globalPos[cid].x, globalPos[cid].y, projectxy ? 0 : globalPos[cid].z);
	if (globalPos[cid].w < id_limit) globalPos[cid].w = id;
}

vboList DrawClusters(const AliHLTTPCCATracker &tracker, int select, int iCol)
{
	int iSlice = tracker.ISlice();
	size_t startCount = vertexBufferStart[iSlice].size();
	size_t startCountInner = vertexBuffer[iSlice].size();
	const int firstCluster = (nCollisions > 1 && iCol > 0) ? collisionClusters[iCol - 1][iSlice] : 0;
	const int lastCluster = (nCollisions > 1 && iCol + 1 < nCollisions) ? collisionClusters[iCol][iSlice] : tracker.Data().NumberOfHits();
	for (int cidInSlice = firstCluster;cidInSlice < lastCluster;cidInSlice++)
	{
		const int cid = tracker.ClusterData()->Id(cidInSlice);
		if (hideUnmatchedClusters && SuppressHit(cid)) continue;
		bool draw = globalPos[cid].w == select;

		if (markAdjacentClusters)
		{
			const int attach = merger.ClusterAttachment()[cid];
			if (attach)
			{
				if (markAdjacentClusters >= 16)
				{
					if (clusterRemovable(cid, markAdjacentClusters == 17)) draw = select == 8;
				}
				else if ((markAdjacentClusters & 2) && (attach & AliHLTTPCGMMerger::attachTube)) draw = select == 8;
				else if ((markAdjacentClusters & 1) && (attach & (AliHLTTPCGMMerger::attachGood | AliHLTTPCGMMerger::attachTube)) == 0) draw = select == 8;
				else if ((markAdjacentClusters & 4) && (attach & AliHLTTPCGMMerger::attachGoodLeg) == 0) draw = select == 8;
				else if (markAdjacentClusters & 8)
				{
					if (fabs(merger.OutputTracks()[attach & AliHLTTPCGMMerger::attachTrackMask].GetParam().GetQPt()) > 20.f) draw = select == 8;
				}
			}
		}
		else if (markClusters)
		{
			const short flags = tracker.ClusterData()->Flags(cidInSlice);
			const bool match = flags & markClusters;
			draw = (select == 8) ? (match) : (draw && !match);
		}
		if (draw)
		{
			vertexBuffer[iSlice].emplace_back(globalPos[cid].x, globalPos[cid].y, projectxy ? 0 : globalPos[cid].z);
		}
	}
	insertVertexList(tracker.ISlice(), startCountInner, vertexBuffer[iSlice].size());
	return(vboList(startCount, vertexBufferStart[iSlice].size() - startCount, iSlice));
}

vboList DrawLinks(const AliHLTTPCCATracker &tracker, int id, bool dodown = false)
{
	int iSlice = tracker.ISlice();
	if (config.clustersOnly) return(vboList(0, 0, iSlice));
	size_t startCount = vertexBufferStart[iSlice].size();
	size_t startCountInner = vertexBuffer[iSlice].size();
	for (int i = 0;i < HLTCA_ROW_COUNT;i++)
	{
		const AliHLTTPCCARow &row = tracker.Data().Row(i);

		if (i < HLTCA_ROW_COUNT - 2)
		{
			const AliHLTTPCCARow &rowUp = tracker.Data().Row(i + 2);
			for (int j = 0;j < row.NHits();j++)
			{
				if (tracker.Data().HitLinkUpData(row, j) != CALINK_INVAL)
				{
					const int cid1 = tracker.ClusterData()->Id(tracker.Data().ClusterDataIndex(row, j));
					const int cid2 = tracker.ClusterData()->Id(tracker.Data().ClusterDataIndex(rowUp, tracker.Data().HitLinkUpData(row, j)));
					drawPointLinestrip(iSlice, cid1, id);
					drawPointLinestrip(iSlice, cid2, id);
				}
			}
		}

		if (dodown && i >= 2)
		{
			const AliHLTTPCCARow &rowDown = tracker.Data().Row(i - 2);
			for (int j = 0;j < row.NHits();j++)
			{
				if (tracker.Data().HitLinkDownData(row, j) != CALINK_INVAL)
				{
					const int cid1 = tracker.ClusterData()->Id(tracker.Data().ClusterDataIndex(row, j));
					const int cid2 = tracker.ClusterData()->Id(tracker.Data().ClusterDataIndex(rowDown, tracker.Data().HitLinkDownData(row, j)));
					drawPointLinestrip(iSlice, cid1, id);
					drawPointLinestrip(iSlice, cid2, id);
				}
				}
		}
	}
	insertVertexList(tracker.ISlice(), startCountInner, vertexBuffer[iSlice].size());
	return(vboList(startCount, vertexBufferStart[iSlice].size() - startCount, iSlice));
}

vboList DrawSeeds(const AliHLTTPCCATracker &tracker)
{
	int iSlice = tracker.ISlice();
	if (config.clustersOnly) return(vboList(0, 0, iSlice));
	size_t startCount = vertexBufferStart[iSlice].size();
	for (int i = 0;i < *tracker.NTracklets();i++)
	{
		const AliHLTTPCCAHitId &hit = tracker.TrackletStartHit(i);
		size_t startCountInner = vertexBuffer[iSlice].size();
		int ir = hit.RowIndex();
		calink ih = hit.HitIndex();
		do
		{
			const AliHLTTPCCARow &row = tracker.Data().Row(ir);
			const int cid = tracker.ClusterData()->Id(tracker.Data().ClusterDataIndex(row, ih));
			drawPointLinestrip(iSlice, cid, 3);
			ir += 2;
			ih = tracker.Data().HitLinkUpData(row, ih);
		} while (ih != CALINK_INVAL);
		insertVertexList(tracker.ISlice(), startCountInner, vertexBuffer[iSlice].size());
	}
	return(vboList(startCount, vertexBufferStart[iSlice].size() - startCount, iSlice));
}

vboList DrawTracklets(const AliHLTTPCCATracker &tracker)
{
	int iSlice = tracker.ISlice();
	if (config.clustersOnly) return(vboList(0, 0, iSlice));
	size_t startCount = vertexBufferStart[iSlice].size();
	for (int i = 0;i < *tracker.NTracklets();i++)
	{
		const AliHLTTPCCATracklet &tracklet = tracker.Tracklet(i);
		if (tracklet.NHits() == 0) continue;
		size_t startCountInner = vertexBuffer[iSlice].size();
		float4 oldpos;
		for (int j = tracklet.FirstRow();j <= tracklet.LastRow();j++)
		{
#ifdef EXTERN_ROW_HITS
			const calink rowHit = tracker.TrackletRowHits()[j * *tracker.NTracklets() + i];
#else
			const calink rowHit = tracklet.RowHit(j);
#endif
			if (rowHit != CALINK_INVAL)
			{
				const AliHLTTPCCARow &row = tracker.Data().Row(j);
				const int cid = tracker.ClusterData()->Id(tracker.Data().ClusterDataIndex(row, rowHit));
				oldpos = globalPos[cid];
				drawPointLinestrip(iSlice, cid, 4);
			}
		}
		insertVertexList(tracker.ISlice(), startCountInner, vertexBuffer[iSlice].size());
	}
	return(vboList(startCount, vertexBufferStart[iSlice].size() - startCount, iSlice));
}

vboList DrawTracks(const AliHLTTPCCATracker &tracker, int global)
{
	int iSlice = tracker.ISlice();
	if (config.clustersOnly) return(vboList(0, 0, iSlice));
	size_t startCount = vertexBufferStart[iSlice].size();
	for (int i = (global ? tracker.CommonMemory()->fNLocalTracks : 0);i < (global ? *tracker.NTracks() : tracker.CommonMemory()->fNLocalTracks);i++)
	{
		AliHLTTPCCATrack &track = tracker.Tracks()[i];
		size_t startCountInner = vertexBuffer[iSlice].size();
		for (int j = 0;j < track.NHits();j++)
		{
			const AliHLTTPCCAHitId &hit = tracker.TrackHits()[track.FirstHitID() + j];
			const AliHLTTPCCARow &row = tracker.Data().Row(hit.RowIndex());
			const int cid = tracker.ClusterData()->Id(tracker.Data().ClusterDataIndex(row, hit.HitIndex()));
			drawPointLinestrip(iSlice, cid, 5 + global);
		}
		insertVertexList(tracker.ISlice(), startCountInner, vertexBuffer[iSlice].size());
	}
	return(vboList(startCount, vertexBufferStart[iSlice].size() - startCount, iSlice));
}

void DrawFinal(int iSlice, int /*iCol*/, AliHLTTPCGMPropagator* prop, std::array<vecpod<int>, 2>& trackList, threadVertexBuffer& threadBuffer)
{
	auto& vBuf = threadBuffer.vBuf;
	auto& buffer = threadBuffer.buffer;
	unsigned int nTracks = std::max(trackList[0].size(), trackList[1].size());
	if (config.clustersOnly) nTracks = 0;
	for (unsigned int ii = 0;ii < nTracks;ii++)
	{
		int i = 0;
		const AliHLTTPCGMMergedTrack* track = nullptr;
		int lastCluster = -1;
		while (true)
		{
			if (ii >= trackList[0].size()) break;
			i = trackList[0][ii];
			track = &merger.OutputTracks()[i];

			size_t startCountInner = vertexBuffer[iSlice].size();
			bool drawing = false;
			for (int k = 0;k < track->NClusters();k++)
			{
				if (hideRejectedClusters && (merger.Clusters()[track->FirstClusterRef() + k].fState & AliHLTTPCGMMergedTrackHit::flagReject)) continue;
				int cid = merger.Clusters()[track->FirstClusterRef() + k].fNum;
				if (drawing) drawPointLinestrip(iSlice, cid, 7, SEPERATE_GLOBAL_TRACKS_LIMIT);
				if (globalPos[cid].w == SEPERATE_GLOBAL_TRACKS_LIMIT)
				{
					if (drawing) insertVertexList(vBuf[0], startCountInner, vertexBuffer[iSlice].size());
					drawing = false;
				}
				else
				{
					if (!drawing) startCountInner = vertexBuffer[iSlice].size();
					if (!drawing) drawPointLinestrip(iSlice, cid, 7, SEPERATE_GLOBAL_TRACKS_LIMIT);
					if (!drawing && lastCluster != -1) drawPointLinestrip(iSlice, merger.Clusters()[track->FirstClusterRef() + lastCluster].fNum, 7, SEPERATE_GLOBAL_TRACKS_LIMIT);
					drawing = true;
				}
				lastCluster = k;
			}
			insertVertexList(vBuf[0], startCountInner, vertexBuffer[iSlice].size());
			break;
		}

		for (int iMC = 0;iMC < 2;iMC++)
		{
			if (iMC)
			{
				if (ii >= trackList[1].size()) continue;
				i = trackList[1][ii];
			}
			else
			{
				if (track == nullptr) continue;
				if (lastCluster == -1) continue;
			}

			size_t startCountInner = vertexBuffer[iSlice].size();
			for (int inFlyDirection = 0;inFlyDirection < 2;inFlyDirection++)
			{
				AliHLTTPCGMPhysicalTrackModel param;
				float ZOffset = 0;
				float x = 0;
				int slice = iSlice;
				float alpha = hlt.Param().Alpha(slice);
				if (iMC == 0)
				{
					param.Set(track->GetParam());
					ZOffset = track->GetParam().GetZOffset();
					auto cl = merger.Clusters()[track->FirstClusterRef() + lastCluster];
					x = cl.fX;
				}
				else
				{
					const AliHLTTPCCAMCInfo& mc = hlt.GetMCInfo()[i];
					if (mc.fCharge == 0.f) break;
					if (mc.fPID < 0) break;

					float c = cosf(alpha);
					float s = sinf(alpha);
					float mclocal[4];
					x = mc.fX;
					float y = mc.fY;
					mclocal[0] = x*c + y*s;
					mclocal[1] =-x*s + y*c;
					float px = mc.fPx;
					float py = mc.fPy;
					mclocal[2] = px*c + py*s;
					mclocal[3] =-px*s + py*c;
					float charge = mc.fCharge > 0 ? 1.f : -1.f;

					x = mclocal[0];
					if (fabs(mc.fZ) > 250) ZOffset = mc.fZ > 0 ? (mc.fZ - 250) : (mc.fZ + 250);
					param.Set(mclocal[0], mclocal[1], mc.fZ - ZOffset, mclocal[2], mclocal[3], mc.fPz, charge);
				}
				param.X() += Xadd;
				x += Xadd;
				float z0 = param.Z();
				if (iMC && inFlyDirection == 0) buffer.clear();
				if (x < 1) break;
				if (fabs(param.SinPhi()) > 1) break;
				alpha = hlt.Param().Alpha(slice);
				vecpod<GLvertex>& useBuffer = iMC && inFlyDirection == 0 ? buffer : vertexBuffer[iSlice];
				int nPoints = 0;

				while (nPoints++ < 5000)
				{
					if ((inFlyDirection == 0 && x < 0) || (inFlyDirection && x * x + param.Y() * param.Y() > (iMC ? (450 * 450) : (300 * 300)))) break;
					if (fabs(param.Z() + ZOffset) > maxClusterZ + (iMC ? 0 : 0)) break;
					if (fabs(param.Z() - z0) > (iMC ? 250 : 250)) break;
					if (inFlyDirection)
					{
						if (fabs(param.SinPhi()) > 0.4)
						{
							float dalpha = asinf(param.SinPhi());
							param.Rotate(dalpha);
							alpha += dalpha;
						}
						x = param.X() + 1.f;
						if (!propagateLoopers)
						{
							float diff = fabs(alpha - hlt.Param().Alpha(slice)) / (2. * M_PI);
							diff -= floor(diff);
							if (diff > 0.25 && diff < 0.75) break;
						}
					}
					float B[3];
					prop->GetBxByBz(alpha, param.GetX(), param.GetY(), param.GetZ(), B );
					float dLp=0;
					if (param.PropagateToXBxByBz(x, B[0], B[1], B[2], dLp)) break;
					if (fabs(param.SinPhi()) > 0.9) break;
					float sa = sinf(alpha), ca = cosf(alpha);
					useBuffer.emplace_back((ca * param.X() - sa * param.Y()) / GL_SCALE_FACTOR, (ca * param.Y() + sa * param.X()) / GL_SCALE_FACTOR, projectxy ? 0 : (param.Z() + ZOffset) / GL_SCALE_FACTOR);
					x += inFlyDirection ? 1 : -1;
				}

				if (inFlyDirection == 0)
				{
					if (iMC)
					{
						for (int k = (int) buffer.size() - 1;k >= 0;k--)
						{
							vertexBuffer[iSlice].emplace_back(buffer[k]);
						}
					}
					else
					{
						insertVertexList(vBuf[1], startCountInner, vertexBuffer[iSlice].size());
						startCountInner = vertexBuffer[iSlice].size();
					}
				}
			}
			insertVertexList(vBuf[iMC ? 3 : 2], startCountInner, vertexBuffer[iSlice].size());
		}
	}
}

vboList DrawGrid(const AliHLTTPCCATracker &tracker)
{
	int iSlice = tracker.ISlice();
	size_t startCount = vertexBufferStart[iSlice].size();
	size_t startCountInner = vertexBuffer[iSlice].size();
	for (int i = 0;i < HLTCA_ROW_COUNT;i++)
	{
		const AliHLTTPCCARow &row = tracker.Data().Row(i);
		for (int j = 0;j <= (signed) row.Grid().Ny();j++)
		{
			float z1 = row.Grid().ZMin();
			float z2 = row.Grid().ZMax();
			float x = row.X() + Xadd;
			float y = row.Grid().YMin() + (float) j / row.Grid().StepYInv();
			float zz1, zz2, yy1, yy2, xx1, xx2;
			tracker.Param().Slice2Global(tracker.ISlice(), x, y, z1, &xx1, &yy1, &zz1);
			tracker.Param().Slice2Global(tracker.ISlice(), x, y, z2, &xx2, &yy2, &zz2);
			if (iSlice < 18)
			{
				zz1 += Zadd;
				zz2 += Zadd;
			}
			else
			{
				zz1 -= Zadd;
				zz2 -= Zadd;
			}
			vertexBuffer[iSlice].emplace_back(xx1 / GL_SCALE_FACTOR, yy1 / GL_SCALE_FACTOR, zz1 / GL_SCALE_FACTOR);
			vertexBuffer[iSlice].emplace_back(xx2 / GL_SCALE_FACTOR, yy2 / GL_SCALE_FACTOR, zz2 / GL_SCALE_FACTOR);
		}
		for (int j = 0;j <= (signed) row.Grid().Nz();j++)
		{
			float y1 = row.Grid().YMin();
			float y2 = row.Grid().YMax();
			float x = row.X() + Xadd;
			float z = row.Grid().ZMin() + (float) j / row.Grid().StepZInv();
			float zz1, zz2, yy1, yy2, xx1, xx2;
			tracker.Param().Slice2Global(tracker.ISlice(), x, y1, z, &xx1, &yy1, &zz1);
			tracker.Param().Slice2Global(tracker.ISlice(), x, y2, z, &xx2, &yy2, &zz2);
			if (iSlice < 18)
			{
				zz1 += Zadd;
				zz2 += Zadd;
			}
			else
			{
				zz1 -= Zadd;
				zz2 -= Zadd;
			}
			vertexBuffer[iSlice].emplace_back(xx1 / GL_SCALE_FACTOR, yy1 / GL_SCALE_FACTOR, zz1 / GL_SCALE_FACTOR);
			vertexBuffer[iSlice].emplace_back(xx2 / GL_SCALE_FACTOR, yy2 / GL_SCALE_FACTOR, zz2 / GL_SCALE_FACTOR);
		}
	}
	insertVertexList(tracker.ISlice(), startCountInner, vertexBuffer[iSlice].size());
	return(vboList(startCount, vertexBufferStart[iSlice].size() - startCount, iSlice));
}

int DrawGLScene(bool mixAnimation, float animateTime) // Here's Where We Do All The Drawing
{
	static float fpsscale = 1, fpsscaleadjust = 0;

	static int framesDone = 0, framesDoneFPS = 0;
	static HighResTimer timerFPS, timerDisplay, timerDraw;
	bool showTimer = false;

	static vboList glDLlines[fgkNSlices][N_LINES_TYPE];
	static vecpod<std::array<vboList, N_FINAL_TYPE>> glDLfinal[fgkNSlices];
	static vecpod<vboList> GLpoints[fgkNSlices][N_POINTS_TYPE];
	static vboList glDLgrid[fgkNSlices];

	//Make sure event gets not overwritten during display
	if (animateTime < 0)
	{
		#ifdef WIN32
			WaitForSingleObject(semLockDisplay, INFINITE);
		#else
			pthread_mutex_lock(&semLockDisplay);
		#endif
	}

	if (!mixAnimation && offscreenBuffer.created)
	{
		setFrameBuffer(1, offscreenBuffer.fb_id);
	}

	//Initialize
	if (!mixAnimation)
	{
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // Clear Screen And Depth Buffer
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();                                   // Reset The Current Modelview Matrix
	}

	int mouseWheelTmp = mouseWheel;
	mouseWheel = 0;
	bool lookOrigin = camLookOrigin ^ keys[KEY_ALT];
	bool yUp = camYUp ^ keys[KEY_CTRL] ^ lookOrigin;

	//Calculate rotation / translation scaling factors
	float scalefactor = keys[KEY_SHIFT] ? 0.2 : 1.0;
	float rotatescalefactor = scalefactor * 0.25f;
	if (cfg.drawSlice != -1)
	{
		scalefactor *= 0.2f;
	}
	float sqrdist = sqrtf(sqrtf(currentMatrix[12] * currentMatrix[12] + currentMatrix[13] * currentMatrix[13] + currentMatrix[14] * currentMatrix[14]) / GL_SCALE_FACTOR) * 0.8;
	if (sqrdist < 0.2) sqrdist = 0.2;
	if (sqrdist > 5) sqrdist = 5;
	scalefactor *= sqrdist;

	float mixSlaveImage = 0.f;
	float time = animateTime;
	if (animate && time < 0)
	{
		if (animateScreenshot) time = animationFrame / 30.f;
		else time = animationTimer.GetCurrentElapsedTime();

		float maxTime = animateVectors[0].back();
		animationFrame++;
		if (time >= maxTime)
		{
			time = maxTime;
			animate = 0;
			SetInfo("Animation finished. (%1.2f seconds, %d frames)", time, animationFrame);
		}
		else
		{
			SetInfo("Running animation: time %1.2f/%1.2f, frames %d", time, maxTime, animationFrame);
		}
	}
	//Perform new rotation / translation
	if (animate)
	{
		float vals[8];
		for (int i = 0;i < 8;i++)
		{
			vals[i] = animationSplines[i].evaluate(time);
		}
		if (animationChangeConfig && mixAnimation == false)
		{
			int base = 0;
			int k = animateVectors[0].size() - 1;
			while (base < k && time > animateVectors[0][base]) base++;
			if (base > animationLastBase + 1) animationLastBase = base - 1;

			if (base != animationLastBase && animateVectors[0][animationLastBase] != animateVectors[0][base] && memcmp(&animateConfig[base], &animateConfig[animationLastBase], sizeof(animateConfig[base])))
			{
				cfg = animateConfig[animationLastBase];
				updateConfig();
				if (drawQualityRenderToTexture)
				{
					setFrameBuffer(1, mixBuffer.fb_id);
					glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); //Clear Screen And Depth Buffer
					DrawGLScene(true, time);
					setFrameBuffer();
				}
				else
				{
					DrawGLScene(true, time);
					CHKERR(glBlitNamedFramebuffer(mainBufferStack.back(), mixBuffer.fb_id, 0, 0, render_width, render_height, 0, 0, render_width, render_height, GL_COLOR_BUFFER_BIT, GL_NEAREST));
					glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); //Clear Screen And Depth Buffer
				}
				glMatrixMode(GL_MODELVIEW);
				glLoadIdentity();                                   //Reset The Current Modelview Matrix
				mixSlaveImage = 1.f - (time - animateVectors[0][animationLastBase]) / (animateVectors[0][base] - animateVectors[0][animationLastBase]);
			}

			if (memcmp(&animateConfig[base], &cfg, sizeof(cfg)))
			{
				cfg = animateConfig[base];
				updateConfig();
			}
		}

		if (cfg.animationMode != 6)
		{
			if (cfg.animationMode & 1) //Rotation from euler angles
			{
				glRotatef(-vals[4] * 180.f / M_PI, 1, 0, 0);
				glRotatef(vals[5] * 180.f / M_PI, 0, 1, 0);
				glRotatef(-vals[6] * 180.f / M_PI, 0, 0, 1);
			}
			else //Rotation from quaternion
			{
				const float mag = sqrtf(vals[4] * vals[4] + vals[5] * vals[5] + vals[6] * vals[6] + vals[7] * vals[7]);
				if (mag < 0.0001) vals[7] = 1;
				else for (int i = 0;i < 4;i++) vals[4 + i] /= mag;

				float xx = vals[4] * vals[4], xy = vals[4] * vals[5], xz = vals[4] * vals[6], xw = vals[4] * vals[7], yy = vals[5] * vals[5], yz = vals[5] * vals[6], yw = vals[5] * vals[7], zz = vals[6] * vals[6], zw = vals[6] * vals[7];
				float mat[16] = {1 - 2 * (yy + zz), 2 * (xy - zw), 2 * (xz + yw), 0, 2 * (xy + zw),  1 - 2 * (xx + zz), 2 * (yz - xw), 0, 2 * (xz - yw), 2 * (yz + xw), 1 - 2 * (xx + yy), 0, 0, 0, 0, 1};
				glMultMatrixf(mat);
			}
		}
		if (cfg.animationMode & 4) //Compute cartesian translation from sperical coordinates (euler angles)
		{
			const float r = vals[3], phi = vals[1], theta = vals[2];
			vals[2] = r * cosf(phi) * cosf(theta);
			vals[0] = r * sinf(phi) * cosf(theta);
			vals[1] = r * sinf(theta);
		}
		else if (cfg.animationMode & 2) //Scale cartesion translation to interpolated radius
		{
			float r = sqrtf(vals[0] * vals[0] + vals[1] * vals[1] + vals[2] * vals[2]);
			if (fabs(r) < 0.0001) r = 1;
			r = vals[3] / r;
			for (int i = 0;i < 3;i++) vals[i] *= r;
		}
		if (cfg.animationMode == 6)
		{
			gluLookAt(vals[0], vals[1], vals[2], 0, 0, 0, 0, 1, 0);
		}
		else
		{
			glTranslatef(-vals[0], -vals[1], -vals[2]);
		}
	}
	else if (resetScene)
	{
		glTranslatef(0, 0, -8);

		cfg.pointSize = 2.0;
		cfg.drawSlice = -1;
		Xadd = Zadd = 0;
		camLookOrigin = camYUp = false;
		angleRollOrigin = -1e9;

		resetScene = 0;
		updateDLList = true;
	}
	else
	{
		float moveZ = scalefactor * ((float) mouseWheelTmp / 150 + (float) (keys['W'] - keys['S']) * (!keys[KEY_SHIFT]) * 0.2 * fpsscale);
		float moveY = scalefactor * ((float) (keys[KEY_PAGEDOWN] - keys[KEY_PAGEUP]) * 0.2 * fpsscale);
		float moveX = scalefactor * ((float) (keys['A'] - keys['D']) * (!keys[KEY_SHIFT]) * 0.2 * fpsscale);
		float rotRoll = rotatescalefactor * fpsscale * 2 * (keys['E'] - keys['F']) * (!keys[KEY_SHIFT]);
		float rotYaw = rotatescalefactor * fpsscale * 2 * (keys[KEY_RIGHT] - keys[KEY_LEFT]);
		float rotPitch = rotatescalefactor * fpsscale * 2 * (keys[KEY_DOWN] - keys[KEY_UP]);

		if (mouseDnR && mouseDn)
		{
			moveZ += -scalefactor * ((float) mouseMvY - (float) mouseDnY) / 4;
			rotRoll += rotatescalefactor * ((float) mouseMvX - (float) mouseDnX);
		}
		else if (mouseDnR)
		{
			moveX += -scalefactor * 0.5 * ((float) mouseDnX - (float) mouseMvX) / 4;
			moveY += -scalefactor * 0.5 * ((float) mouseMvY - (float) mouseDnY) / 4;
		}
		else if (mouseDn)
		{
			rotYaw += rotatescalefactor * ((float) mouseMvX - (float) mouseDnX);
			rotPitch += rotatescalefactor * ((float) mouseMvY - (float) mouseDnY);
		}

		if (keys['<'] && !keysShift['<'])
		{
			animationDelay += moveX;
			if (animationDelay < 0.05) animationDelay = 0.05;
			moveX = 0.f;
			moveY = 0.f;
			SetInfo("Animation delay set to %1.2f", animationDelay);
		}

		if (yUp) angleRollOrigin = 0;
		else if (!lookOrigin) angleRollOrigin = -1e6;
		if (lookOrigin)
		{
			if (!yUp)
			{
				if (angleRollOrigin < -1e6) angleRollOrigin = yUp ? 0. : -angle[2];
				angleRollOrigin += rotRoll;
				glRotatef(angleRollOrigin, 0, 0, 1);
				float tmpX = moveX, tmpY = moveY;
				moveX = tmpX * cosf(angle[2]) - tmpY * sinf(angle[2]);
				moveY = tmpX * sinf(angle[2]) + tmpY * cosf(angle[2]);
			}

			const float x = xyz[0], y = xyz[1], z = xyz[2];
			float r = sqrtf(x * x + + y * y + z * z);
			float r2 = sqrtf(x * x + z * z);
			float phi = atan2f(z, x);
			phi += moveX * 0.1f;
			float theta = atan2f(xyz[1], r2);
			theta -= moveY * 0.1f;
			const float max_theta = M_PI / 2 - 0.01;
			if (theta >= max_theta) theta = max_theta;
			else if (theta <= -max_theta) theta = -max_theta;
			if (moveZ >= r - 0.1) moveZ = r - 0.1;
			r -= moveZ;
			r2 = r * cosf(theta);
			xyz[0] = r2 * cosf(phi);
			xyz[2] = r2 * sinf(phi);
			xyz[1] = r * sinf(theta);

			gluLookAt(xyz[0], xyz[1], xyz[2], 0, 0, 0, 0, 1, 0);
		}
		else
		{
			glTranslatef(moveX, moveY, moveZ);
			if (rotYaw != 0.f) glRotatef(rotYaw, 0, 1, 0);
			if (rotPitch != 0.f) glRotatef(rotPitch, 1, 0, 0);
			if (!yUp && rotRoll != 0.f) glRotatef(rotRoll, 0, 0, 1);
			glMultMatrixf(currentMatrix); //Apply previous translation / rotation

			if (yUp)
			{
				glGetFloatv(GL_MODELVIEW_MATRIX, currentMatrix);
				calcXYZ();
				glLoadIdentity();
				glRotatef(angle[2] * 180.f / M_PI, 0, 0, 1);
				glMultMatrixf(currentMatrix);
			}
		}

		//Graphichs Options
		float minSize = 0.4 / (drawQualityDownsampleFSAA > 1 ? drawQualityDownsampleFSAA : 1);
		int deltaLine = keys['+']*keysShift['+'] - keys['-']*keysShift['-'];
		cfg.lineWidth += (float) deltaLine * fpsscale * 0.02 * cfg.lineWidth;
		if (cfg.lineWidth < minSize) cfg.lineWidth = minSize;
		if (deltaLine) SetInfo("%s line width: %f", deltaLine > 0 ? "Increasing" : "Decreasing", cfg.lineWidth);
		minSize *= 2;
		int deltaPoint = keys['+']*(!keysShift['+']) - keys['-']*(!keysShift['-']);
		cfg.pointSize += (float) deltaPoint * fpsscale * 0.02 * cfg.pointSize;
		if (cfg.pointSize < minSize) cfg.pointSize = minSize;
		if (deltaPoint) SetInfo("%s point size: %f", deltaPoint > 0 ? "Increasing" : "Decreasing", cfg.pointSize);
	}

	//Store position
	if (animateTime < 0)
	{
		glGetFloatv(GL_MODELVIEW_MATRIX, currentMatrix);
		calcXYZ();
	}

	if (mouseDn || mouseDnR)
	{
		mouseDnX = mouseMvX;
		mouseDnY = mouseMvY;
	}

	//Open GL Default Values
	if (cfg.smoothPoints) CHKERR(glEnable(GL_POINT_SMOOTH))
	else CHKERR(glDisable(GL_POINT_SMOOTH))
	if (cfg.smoothLines) CHKERR(glEnable(GL_LINE_SMOOTH))
	else CHKERR(glDisable(GL_LINE_SMOOTH))
	CHKERR(glEnable(GL_BLEND));
	CHKERR(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
	CHKERR(glPointSize(cfg.pointSize * (drawQualityDownsampleFSAA > 1 ? drawQualityDownsampleFSAA : 1)));
	CHKERR(glLineWidth(cfg.lineWidth * (drawQualityDownsampleFSAA > 1 ? drawQualityDownsampleFSAA : 1)));

	//Extract global cluster information
	if (updateDLList || displayEventNr != currentEventNr)
	{
		showTimer = true;
		timerDraw.ResetStart();
		currentClusters = 0;
		for (int iSlice = 0;iSlice < fgkNSlices;iSlice++)
		{
			currentClusters += hlt.Tracker().CPUTracker(iSlice).NHitsTotal();
		}

		if (maxClusters < currentClusters)
		{
			maxClusters = currentClusters;
			globalPosPtr.reset(new float4[maxClusters]);
			globalPos = globalPosPtr.get();
		}

		maxClusterZ = 0;
		for (int iSlice = 0;iSlice < fgkNSlices;iSlice++)
		{
			const AliHLTTPCCAClusterData &cdata = hlt.ClusterData(iSlice);
			for (int i = 0;i < cdata.NumberOfClusters();i++)
			{
				const int cid = cdata.Id(i);
				if (cid >= maxClusters)
				{
					printf("Cluster Buffer Size exceeded (id %d max %d)\n", cid, maxClusters);
					exit(1);
				}
				float4 *ptr = &globalPos[cid];
				hlt.Tracker().CPUTracker(iSlice).Param().Slice2Global(iSlice, cdata.X(i) + Xadd, cdata.Y(i), cdata.Z(i), &ptr->x, &ptr->y, &ptr->z);
				if (fabs(ptr->z) > maxClusterZ) maxClusterZ = fabs(ptr->z);
				if (iSlice < 18)
				{
					ptr->z += Zadd;
					ptr->z += Zadd;
				}
				else
				{
					ptr->z -= Zadd;
					ptr->z -= Zadd;
				}

				ptr->x /= GL_SCALE_FACTOR;
				ptr->y /= GL_SCALE_FACTOR;
				ptr->z /= GL_SCALE_FACTOR;
				ptr->w = 1;
			}
		}

		currentEventNr = displayEventNr;

		timerFPS.ResetStart();
		framesDoneFPS = 0;
		fpsscaleadjust = 0;
		glDLrecent = 0;
		updateDLList = 0;
	}

	//Prepare Event
	if (!glDLrecent)
	{
		for (int i = 0;i < fgkNSlices;i++)
		{
			vertexBuffer[i].clear();
			vertexBufferStart[i].clear();
			vertexBufferCount[i].clear();
		}

		for (int i = 0;i < currentClusters;i++) globalPos[i].w = 0;

		for (int iSlice = 0;iSlice < fgkNSlices;iSlice++)
		{
			for (int i = 0;i < N_POINTS_TYPE;i++) GLpoints[iSlice][i].resize(nCollisions);
			for (int i = 0;i < N_FINAL_TYPE;i++) glDLfinal[iSlice].resize(nCollisions);
		}
#ifdef HLTCA_HAVE_OPENMP
#pragma omp parallel
		{
			int numThread = omp_get_thread_num();
			int numThreads = omp_get_num_threads();
#pragma omp for
#else
		{
			int numThread = 0, numThreads = 1;
#endif
			for (int iSlice = 0;iSlice < fgkNSlices;iSlice++)
			{
				AliHLTTPCCATracker &tracker = (AliHLTTPCCATracker&) hlt.Tracker().CPUTracker(iSlice);
				char *tmpMem;
				if (tracker.fLinkTmpMemory == NULL)
				{
					if (tracker.Data().NumberOfHits()) printf("Need to set TRACKER_KEEP_TEMPDATA for visualizing PreLinks!\n");
					continue;
				}
				tmpMem = tracker.Data().Memory();
				tracker.SetGPUSliceDataMemory((void *) tracker.fLinkTmpMemory, tracker.Data().Rows());
				tracker.SetPointersSliceData(tracker.ClusterData());
				glDLlines[iSlice][0] = DrawLinks(tracker, 1, true);
				tracker.SetGPUSliceDataMemory(tmpMem, tracker.Data().Rows());
				tracker.SetPointersSliceData(tracker.ClusterData());
			}
			AliHLTTPCGMPropagator prop;
			const float kRho = 1.025e-3;//0.9e-3;
			const float kRadLen = 29.532;//28.94;
			prop.SetMaxSinPhi(.999);
			prop.SetMaterial(kRadLen, kRho);
			prop.SetPolynomialField(merger.pField());
			prop.SetToyMCEventsFlag(merger.SliceParam().ToyMCEventsFlag);

#ifdef HLTCA_HAVE_OPENMP
#pragma omp barrier
#pragma omp for
#endif
			for (int iSlice = 0;iSlice < fgkNSlices;iSlice++)
			{
				const AliHLTTPCCATracker &tracker = hlt.Tracker().CPUTracker(iSlice);

				glDLlines[iSlice][1] = DrawLinks(tracker, 2);
				glDLlines[iSlice][2] = DrawSeeds(tracker);
				glDLlines[iSlice][3] = DrawTracklets(tracker);
				glDLlines[iSlice][4] = DrawTracks(tracker, 0);
				glDLgrid[iSlice] = DrawGrid(tracker);
			}

#ifdef HLTCA_HAVE_OPENMP
#pragma omp barrier
#pragma omp for
#endif
			for (int iSlice = 0;iSlice < fgkNSlices;iSlice++)
			{
				const AliHLTTPCCATracker &tracker = hlt.Tracker().CPUTracker(iSlice);
				glDLlines[iSlice][5] = DrawTracks(tracker, 1);
			}

#ifdef HLTCA_HAVE_OPENMP
#pragma omp barrier
#endif
			threadTracks[numThread].resize(nCollisions);
			for (int i = 0;i < nCollisions;i++) for (int j = 0;j < fgkNSlices;j++) for (int k = 0;k < 2;k++) threadTracks[numThread][i][j][k].clear();
#ifdef HLTCA_HAVE_OPENMP
#pragma omp for
#endif
			for (int i = 0;i < merger.NOutputTracks();i++)
			{
				const AliHLTTPCGMMergedTrack* track = &merger.OutputTracks()[i];
				if (track->NClusters() == 0) continue;
				if (hideRejectedTracks && !track->OK()) continue;
				int slice = merger.Clusters()[track->FirstClusterRef() + track->NClusters() - 1].fSlice;
				unsigned int col = 0;
				if (nCollisions > 1)
				{
					int label = GetMCLabel(i);
					if (label != -1e9 && label < -1) label = -label - 2;
					while (col < collisionClusters.size() && collisionClusters[col][fgkNSlices] < label) col++;
				}
				threadTracks[numThread][col][slice][0].emplace_back(i);
			}
#ifdef HLTCA_HAVE_OPENMP
#pragma omp for
#endif
			for (int i = 0;i < hlt.GetNMCInfo();i++)
			{
				const AliHLTTPCCAMCInfo& mc = hlt.GetMCInfo()[i];
				if (mc.fCharge == 0.f) continue;
				if (mc.fPID < 0) continue;

				float alpha = atan2f(mc.fY, mc.fX);
				if (alpha < 0) alpha += 2 * M_PI;
				int slice = alpha / (2 * M_PI) * 18;
				if (mc.fZ < 0) slice += 18;
				unsigned int col = 0;
				if (nCollisions > 1)
				{
					while (col < collisionClusters.size() && collisionClusters[col][fgkNSlices] < i) col++;
				}
				threadTracks[numThread][col][slice][1].emplace_back(i);
			}
#ifdef HLTCA_HAVE_OPENMP
#pragma omp barrier
#pragma omp for
#endif
			for (int iSlice = 0;iSlice < fgkNSlices;iSlice++)
			{
				for (int iCol = 0;iCol < nCollisions;iCol++)
				{
					threadBuffers[numThread].clear();
					for (int iSet = 0;iSet < numThreads;iSet++)
					{
						DrawFinal(iSlice, iCol, &prop, threadTracks[iSet][iCol][iSlice], threadBuffers[numThread]);
					}
					vboList* list = &glDLfinal[iSlice][iCol][0];
					for (int i = 0;i < N_FINAL_TYPE;i++)
					{
						size_t startCount = vertexBufferStart[iSlice].size();
						for (unsigned int j = 0;j < threadBuffers[numThread].start[i].size();j++)
						{
							vertexBufferStart[iSlice].emplace_back(threadBuffers[numThread].start[i][j]);
							vertexBufferCount[iSlice].emplace_back(threadBuffers[numThread].count[i][j]);
						}
						list[i] = vboList(startCount, vertexBufferStart[iSlice].size() - startCount, iSlice);
					}
				}
			}

#ifdef HLTCA_HAVE_OPENMP
#pragma omp barrier
#pragma omp for
#endif
			for (int iSlice = 0;iSlice < fgkNSlices;iSlice++)
			{
				const AliHLTTPCCATracker &tracker = hlt.Tracker().CPUTracker(iSlice);
				for (int i = 0;i < N_POINTS_TYPE;i++)
				{
					for (int iCol = 0;iCol < nCollisions;iCol++)
					{
						GLpoints[iSlice][i][iCol] = DrawClusters(tracker, i, iCol);
					}
				}
			}
		}
//End omp parallel

		glDLrecent = 1;
		size_t totalVertizes = 0;
		for (int i = 0;i < fgkNSlices;i++) totalVertizes += vertexBuffer[i].size();

		useMultiVBO = (totalVertizes * sizeof(vertexBuffer[0][0]) >= 0x100000000ll);
		if (useMultiVBO)
		{
			for (int i = 0;i < fgkNSlices;i++)
			{
				CHKERR(glNamedBufferData(vbo_id[i], vertexBuffer[i].size() * sizeof(vertexBuffer[i][0]), vertexBuffer[i].data(), GL_STATIC_DRAW));
				vertexBuffer[i].clear();
			}
		}
		else
		{
			size_t totalYet = vertexBuffer[0].size();
			vertexBuffer[0].resize(totalVertizes);
			for (int i = 1;i < fgkNSlices;i++)
			{
				for (unsigned int j = 0;j < vertexBufferStart[i].size();j++)
				{
					vertexBufferStart[i][j] += totalYet;
				}
				memcpy(&vertexBuffer[0][totalYet], &vertexBuffer[i][0], vertexBuffer[i].size() * sizeof(vertexBuffer[i][0]));
				totalYet += vertexBuffer[i].size();
				vertexBuffer[i].clear();
			}
			CHKERR(glBindBuffer(GL_ARRAY_BUFFER, vbo_id[0])); //Bind ahead of time, since it is not going to change
			CHKERR(glNamedBufferData(vbo_id[0], totalVertizes * sizeof(vertexBuffer[0][0]), vertexBuffer[0].data(), GL_STATIC_DRAW));
			vertexBuffer[0].clear();
		}

		if (useGLIndirectDraw)
		{
			static vecpod<DrawArraysIndirectCommand> cmds;
			cmds.clear();
			for (int iSlice = 0;iSlice < fgkNSlices;iSlice++)
			{
				indirectSliceOffset[iSlice] = cmds.size();
				for (unsigned int k = 0;k < vertexBufferStart[iSlice].size();k++)
				{
					cmds.emplace_back(vertexBufferCount[iSlice][k], 1, vertexBufferStart[iSlice][k], 0);
				}
			}
			CHKERR(glBufferData(GL_DRAW_INDIRECT_BUFFER, cmds.size() * sizeof(cmds[0]), cmds.data(), GL_STATIC_DRAW));
		}

		if (showTimer)
		{
			printf("Draw time: %'d us (vertices %'lld / %'lld bytes)\n", (int) (timerDraw.GetCurrentElapsedTime() * 1000000.), (long long int) totalVertizes, (long long int) (totalVertizes * sizeof(vertexBuffer[0][0])));
		}
	}

	//Draw Event
	drawCalls = 0;
	CHKERR(glEnableClientState(GL_VERTEX_ARRAY));
	CHKERR(glVertexPointer(3, GL_FLOAT, 0, 0));

	#define LOOP_SLICE for (int iSlice = (cfg.drawSlice == -1 ? 0 : cfg.drawRelatedSlices ? (cfg.drawSlice % 9) : cfg.drawSlice);iSlice < fgkNSlices;iSlice += (cfg.drawSlice == -1 ? 1 : cfg.drawRelatedSlices ? 9 : fgkNSlices))
	#define LOOP_COLLISION for (int iCol = (cfg.showCollision == -1 ? 0 : cfg.showCollision);iCol < nCollisions;iCol += (cfg.showCollision == -1 ? 1 : nCollisions))
	#define LOOP_COLLISION_COL(cmd) LOOP_COLLISION {if (cfg.colorCollisions) SetCollisionColor(iCol); cmd;}

	if (cfg.drawGrid)
	{
		SetColorGrid();
		LOOP_SLICE drawVertices(glDLgrid[iSlice], GL_LINES);
	}
	if (cfg.drawClusters)
	{
		SetColorClusters();
		LOOP_SLICE LOOP_COLLISION_COL(drawVertices(GLpoints[iSlice][0][iCol], GL_POINTS));

		if (cfg.drawInitLinks)
		{
			if (cfg.excludeClusters) goto skip1;
			if (cfg.colorClusters) SetColorInitLinks();
		}
		LOOP_SLICE LOOP_COLLISION_COL(drawVertices(GLpoints[iSlice][1][iCol], GL_POINTS));

		if (cfg.drawLinks)
		{
			if (cfg.excludeClusters) goto skip1;
			if (cfg.colorClusters) SetColorLinks();
		}
		else
		{
			SetColorClusters();
		}
		LOOP_SLICE LOOP_COLLISION_COL(drawVertices(GLpoints[iSlice][2][iCol], GL_POINTS));

		if (cfg.drawSeeds)
		{
			if (cfg.excludeClusters) goto skip1;
			if (cfg.colorClusters) SetColorSeeds();
		}
		LOOP_SLICE LOOP_COLLISION_COL(drawVertices(GLpoints[iSlice][3][iCol], GL_POINTS));

	skip1:
		SetColorClusters();
		if (cfg.drawTracklets)
		{
			if (cfg.excludeClusters) goto skip2;
			if (cfg.colorClusters) SetColorTracklets();
		}
		LOOP_SLICE LOOP_COLLISION_COL(drawVertices(GLpoints[iSlice][4][iCol], GL_POINTS));

		if (cfg.drawTracks)
		{
			if (cfg.excludeClusters) goto skip2;
			if (cfg.colorClusters) SetColorTracks();
		}
		LOOP_SLICE LOOP_COLLISION_COL(drawVertices(GLpoints[iSlice][5][iCol], GL_POINTS));

	skip2:;
		if (cfg.drawGlobalTracks)
		{
			if (cfg.excludeClusters) goto skip3;
			if (cfg.colorClusters) SetColorGlobalTracks();
		}
		else
		{
			SetColorClusters();
		}
		LOOP_SLICE LOOP_COLLISION_COL(drawVertices(GLpoints[iSlice][6][iCol], GL_POINTS));
		SetColorClusters();

		if (cfg.drawFinal && cfg.propagateTracks < 2)
		{
			if (cfg.excludeClusters) goto skip3;
			if (cfg.colorClusters) SetColorFinal();
		}
		LOOP_SLICE LOOP_COLLISION_COL(drawVertices(GLpoints[iSlice][7][iCol], GL_POINTS));
	skip3:;
}

	if (!config.clustersOnly && !cfg.excludeClusters)
	{
		if (cfg.drawInitLinks)
		{
			SetColorInitLinks();
			LOOP_SLICE drawVertices(glDLlines[iSlice][0], GL_LINES);
		}
		if (cfg.drawLinks)
		{
			SetColorLinks();
			LOOP_SLICE drawVertices(glDLlines[iSlice][1], GL_LINES);
		}
		if (cfg.drawSeeds)
		{
			SetColorSeeds();
			LOOP_SLICE drawVertices(glDLlines[iSlice][2], GL_LINE_STRIP);
		}
		if (cfg.drawTracklets)
		{
			SetColorTracklets();
			LOOP_SLICE drawVertices(glDLlines[iSlice][3], GL_LINE_STRIP);
		}
		if (cfg.drawTracks)
		{
			SetColorTracks();
			LOOP_SLICE drawVertices(glDLlines[iSlice][4], GL_LINE_STRIP);
		}
		if (cfg.drawGlobalTracks)
		{
			SetColorGlobalTracks();
			LOOP_SLICE drawVertices(glDLlines[iSlice][5], GL_LINE_STRIP);
		}
		if (cfg.drawFinal)
		{
			SetColorFinal();
			LOOP_SLICE LOOP_COLLISION
			{
				if (cfg.colorCollisions) SetCollisionColor(iCol);
				if (cfg.propagateTracks < 2) drawVertices(glDLfinal[iSlice][iCol][0], GL_LINE_STRIP);
				if (cfg.propagateTracks > 0 && cfg.propagateTracks < 3) drawVertices(glDLfinal[iSlice][iCol][1], GL_LINE_STRIP);
				if (cfg.propagateTracks == 2) drawVertices(glDLfinal[iSlice][iCol][2], GL_LINE_STRIP);
				if (cfg.propagateTracks == 3) drawVertices(glDLfinal[iSlice][iCol][3], GL_LINE_STRIP);
			}
		}
		if (markClusters || markAdjacentClusters)
		{
			SetColorMarked();
			LOOP_SLICE LOOP_COLLISION drawVertices(GLpoints[iSlice][8][iCol], GL_POINTS);
		}
	}

	CHKERR(glDisableClientState(GL_VERTEX_ARRAY));

	if (mixSlaveImage > 0)
	{
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		gluOrtho2D(0.f, render_width, 0.f, render_height);
		CHKERR(glEnable(GL_TEXTURE_2D));
		glDisable(GL_DEPTH_TEST);
		CHKERR(glBindTexture(GL_TEXTURE_2D, mixBuffer.fbCol_id));
		glColor4f(1, 1, 1, mixSlaveImage);
		glBegin(GL_QUADS);
		glTexCoord2f(0, 0);
		glVertex3f(0, 0, 0);
		glTexCoord2f(0, 1);
		glVertex3f(0, render_height, 0);
		glTexCoord2f(1, 1);
		glVertex3f(render_width, render_height, 0);
		glTexCoord2f(1, 0);
		glVertex3f(render_width, 0, 0);
		glEnd();
		glColor4f(1, 1, 1, 0);
		CHKERR(glDisable(GL_TEXTURE_2D));
		setDepthBuffer();
		glPopMatrix();
	}

	if (mixAnimation)
	{
		glColorMask(false, false, false, true);
		glClear(GL_COLOR_BUFFER_BIT);
		glColorMask(true, true, true, true);
	}
	else if (offscreenBuffer.created)
	{
		setFrameBuffer();
		GLuint srcid = offscreenBuffer.fb_id;
		if (drawQualityMSAA > 1 && drawQualityDownsampleFSAA > 1)
		{
			CHKERR(glBlitNamedFramebuffer(srcid, offscreenBufferNoMSAA.fb_id, 0, 0, render_width, render_height, 0, 0, render_width, render_height, GL_COLOR_BUFFER_BIT, GL_LINEAR));
			srcid = offscreenBufferNoMSAA.fb_id;
		}
		CHKERR(glBlitNamedFramebuffer(srcid, mainBufferStack.back(), 0, 0, render_width, render_height, 0, 0, screen_width, screen_height, GL_COLOR_BUFFER_BIT, GL_LINEAR));
	}

	if (animate && animateScreenshot && animateTime < 0)
	{
		char animateScreenshotFile[32];
		sprintf(animateScreenshotFile, "animation%d_%05d.bmp", animationExport, animationFrame);
		DoScreenshot(animateScreenshotFile, time);
	}

	if (animateTime < 0)
	{
		framesDone++;
		framesDoneFPS++;
		double fpstime = timerFPS.GetCurrentElapsedTime();
		char info[1024];
		float fps = (double) framesDoneFPS / fpstime;
		sprintf(info, "FPS: %6.2f (Slice: %d, 1:Clusters %d, 2:Prelinks %d, 3:Links %d, 4:Seeds %d, 5:Tracklets %d, 6:Tracks %d, 7:GTracks %d, 8:Merger %d) (%d frames, %d draw calls) "
			"(X %1.2f Y %1.2f Z %1.2f / R %1.2f Phi %1.1f Theta %1.1f) / Yaw %1.1f Pitch %1.1f Roll %1.1f)",
			fps, cfg.drawSlice, cfg.drawClusters, cfg.drawInitLinks, cfg.drawLinks, cfg.drawSeeds, cfg.drawTracklets, cfg.drawTracks, cfg.drawGlobalTracks, cfg.drawFinal, framesDone, drawCalls,
			xyz[0], xyz[1], xyz[2], rphitheta[0], rphitheta[1] * 180 / M_PI, rphitheta[2] * 180 / M_PI, angle[1] * 180 / M_PI, angle[0] * 180 / M_PI, angle[2] * 180 / M_PI);
		if (fpstime > 1.)
		{
			if (printInfoText & 2) printf("%s\n", info);
			if (fpsscaleadjust++) fpsscale = 60 / fps;
			timerFPS.ResetStart();
			framesDoneFPS = 0;
		}

		if (printInfoText & 1)
		{
			setFrameBuffer(0, 0);
			showInfo(info);
			setFrameBuffer(-2);
		}
	}

//Free event
#ifdef WIN32
	ReleaseSemaphore(semLockDisplay, 1, NULL);
#else
	pthread_mutex_unlock(&semLockDisplay);
#endif

	return(true);
}

void DoScreenshot(char *filename, float animateTime)
{
	int SCALE_Y = screenshot_scale, SCALE_X = screenshot_scale;

	float tmpPointSize = cfg.pointSize;
	float tmpLineWidth = cfg.lineWidth;
	cfg.pointSize *= (float) (SCALE_X + SCALE_Y) / 2.;
	cfg.lineWidth *= (float) (SCALE_X + SCALE_Y) / 2.;

	int oldWidth = screen_width, oldHeight = screen_height;
	GLfb screenshotBuffer;

	bool needBuffer = SCALE_X != 1 || SCALE_Y != 1;

	if (needBuffer)
	{
		deleteFB(mixBuffer);
		screen_width *= SCALE_X;
		screen_height *= SCALE_Y;
		render_width = screen_width;
		render_height = screen_height;
		createFB(screenshotBuffer, 0, 1, false); //Create screenshotBuffer of size screen_width * SCALE, render_width * SCALE
		UpdateOffscreenBuffers(); //Create other buffers of size screen_width * SCALE * downscale, ...
		setFrameBuffer(1, screenshotBuffer.fb_id);
		glViewport(0, 0, render_width, render_height);
		DrawGLScene(false, animateTime);
	}
	size_t size = 4 * screen_width * screen_height;
	unsigned char *pixels = new unsigned char [size];
	CHKERR(glPixelStorei(GL_PACK_ALIGNMENT, 1));
	CHKERR(glReadBuffer(needBuffer ? GL_COLOR_ATTACHMENT0 : GL_BACK));
	CHKERR(glReadPixels(0, 0, screen_width, screen_height, GL_BGRA, GL_UNSIGNED_BYTE, pixels));

	if (filename)
	{
		FILE *fp = fopen(filename, "w+b");

		BITMAPFILEHEADER bmpFH;
		BITMAPINFOHEADER bmpIH;
		memset(&bmpFH, 0, sizeof(bmpFH));
		memset(&bmpIH, 0, sizeof(bmpIH));

		bmpFH.bfType = 19778; //"BM"
		bmpFH.bfSize = sizeof(bmpFH) + sizeof(bmpIH) + size;
		bmpFH.bfOffBits = sizeof(bmpFH) + sizeof(bmpIH);

		bmpIH.biSize = sizeof(bmpIH);
		bmpIH.biWidth = screen_width;
		bmpIH.biHeight = screen_height;
		bmpIH.biPlanes = 1;
		bmpIH.biBitCount = 32;
		bmpIH.biCompression = BI_RGB;
		bmpIH.biSizeImage = size;
		bmpIH.biXPelsPerMeter = 5670;
		bmpIH.biYPelsPerMeter = 5670;

		fwrite(&bmpFH, 1, sizeof(bmpFH), fp);
		fwrite(&bmpIH, 1, sizeof(bmpIH), fp);
		fwrite(pixels, 1, size, fp);
		fclose(fp);
	}
	delete[] pixels;

	cfg.pointSize = tmpPointSize;
	cfg.lineWidth = tmpLineWidth;
	if (needBuffer)
	{
		setFrameBuffer();
		deleteFB(screenshotBuffer);
		screen_width = oldWidth;
		screen_height = oldHeight;
		UpdateOffscreenBuffers();
		glViewport(0, 0, render_width, render_height);
		DrawGLScene(false, animateTime);
	}
}

const char* HelpText[] = {
	"[n] / [SPACE]                 Next event",
	"[q] / [Q] / [ESC]             Quit",
	"[r]                           Reset Display Settings",
	"[l] / [k] / [J]               Draw single slice (next  / previous slice), draw related slices (same plane in phi)",
	"[;] / [:]                     Show splitting of TPC in slices by extruding volume, [:] resets",
	"[#]                           Invert colors",
	"[y] / [Y] / ['] / [X] / [M]   Start Animation, Add / remove animation point, Reset, Cycle mode",
	"[>] / [<]                     Toggle config interpolation during animation / change animation interval (via movement)",
	"[g]                           Draw Grid",
	"[i]                           Project onto XY-plane",
	"[x]                           Exclude Clusters used in the tracking steps enabled for visualization ([1]-[8])",
	"[.]                           Exclude rejected tracks",
	"[c]                           Mark flagged clusters (splitPad = 0x1, splitTime = 0x2, edge = 0x4, singlePad = 0x8, rejectDistance = 0x10, rejectErr = 0x20",
	"[B]                           Mark clusters attached as adjacent",
	"[L] / [K]                     Draw single collisions (next / previous)",
	"[C]                           Colorcode clusters of different collisions",
	"[v]                           Hide rejected clusters from tracks",
	"[b]                           Hide all clusters not belonging or related to matched tracks",
	"[1]                           Show Clusters",
	"[2]                           Show Links that were removed",
	"[3]                           Show Links that remained in Neighbors Cleaner",
	"[4]                           Show Seeds (Start Hits)",
	"[5]                           Show Tracklets",
	"[6]                           Show Tracks (after Tracklet Selector)",
	"[7]                           Show Global Track Segments",
	"[8]                           Show Final Merged Tracks (after Track Merger)",
	"[j]                           Show global tracks as additional segments of final tracks",
	"[E] / [G]                     Extrapolate tracks / loopers",
	"[t] / [T]                     Take Screenshot / Record animation to pictures",
	"[Z]                           Change screenshot resolution (scaling factor)",
	"[S] / [A] / [D]               Enable or disable smoothing of points / smoothing of lines / depth buffer",
	"[W] / [U] / [V]               Toggle anti-aliasing (MSAA at raster level / change downsampling FSAA facot / toggle VSync",
	"[F] / [_] / [R]               Switch fullscreen / Maximized window / FPS rate limiter",
	"[I]                           Enable / disable GL indirect draw",
	"[o] / [p] / [O] / [P]         Save / restore current camera position / animation path",
	"[h]                           Print Help",
	"[H]                           Show info texts",
	"[w] / [s] / [a] / [d]         Zoom / Strafe Left and Right",
	"[pgup] / [pgdn]               Strafe Up and Down",
	"[e] / [f]                     Rotate",
	"[+] / [-]                     Make points thicker / fainter (Hold SHIFT for lines)",
	"[MOUSE 1]                     Look around",
	"[MOUSE 2]                     Shift camera",
	"[MOUSE 1+2]                   Zoom / Rotate",
	"[SHIFT]                       Slow Zoom / Move / Rotate",
	"[ALT] / [CTRL] / [m]          Focus camera on origin / orient y-axis upwards (combine with [SHIFT] to lock) / Cycle through modes",
	"[1] ... [8] / [V]             Enable display of clusters, preseeds, seeds, starthits, tracklets, tracks, global tracks, merged tracks / Show assigned clusters in colors"
	//FREE: u z
};

void PrintHelp()
{
	infoHelpTimer.ResetStart();
	for (unsigned int i = 0;i < sizeof(HelpText) / sizeof(HelpText[0]);i++) printf("%s\n", HelpText[i]);
}

void HandleKeyRelease(int wParam, char key)
{
	keys[wParam] = false;

	if (wParam >= 'A' && wParam <= 'Z')
	{
		if (keysShift[wParam]) wParam &= ~(int) ('a' ^ 'A');
		else wParam |= (int) ('a' ^ 'A');
	}

	if (wParam == 13 || wParam == 'n')
	{
		exitButton = 1;
		SetInfo("Showing next event");
	}
	else if (wParam == 27 || wParam == 'q' || wParam == 'Q')
	{
		exitButton = 2;
		SetInfo("Exiting");
	}
	else if (wParam == 'r')
	{
		resetScene = 1;
		SetInfo("View reset");
	}
	else if (wParam == KEY_ALT && keysShift[KEY_ALT])
	{
		camLookOrigin ^= 1;
		cameraMode = camLookOrigin + 2 * camYUp;
		SetInfo("Camera locked on origin: %s", camLookOrigin ? "enabled" : "disabled");
	}
	else if (wParam == KEY_CTRL && keysShift[KEY_CTRL])
	{
		camYUp ^= 1;
		cameraMode = camLookOrigin + 2 * camYUp;
		SetInfo("Camera locked on y-axis facing upwards: %s", camYUp ? "enabled" : "disabled");
	}
	else if (wParam == 'm')
	{
		cameraMode++;
		if (cameraMode == 4) cameraMode = 0;
		camLookOrigin = cameraMode & 1;
		camYUp = cameraMode & 2;
		const char* modeText[] = {"Descent (free movement)", "Focus locked on origin (y-axis forced upwards)", "Spectator (y-axis forced upwards)", "Focus locked on origin (with free rotation)"};
		SetInfo("Camera mode %d: %s", cameraMode, modeText[cameraMode]);
	}
	else if (wParam == KEY_ALT)
	{
		keys[KEY_CTRL] = false; //Release CTRL with alt, to avoid orienting along y automatically!
	}
	else if (wParam == 'l')
	{
		if (cfg.drawSlice >= (cfg.drawRelatedSlices ? (fgkNSlices / 4 - 1) : (fgkNSlices - 1)))
		{
			cfg.drawSlice = -1;
			SetInfo("Showing all slices");
		}
		else
		{
			cfg.drawSlice++;
			SetInfo("Showing slice %d", cfg.drawSlice);
		}
	}
	else if (wParam == 'k')
	{
		if (cfg.drawSlice <= -1)
		{
			cfg.drawSlice = cfg.drawRelatedSlices ? (fgkNSlices / 4 - 1) : (fgkNSlices - 1);
		}
		else
		{
			cfg.drawSlice--;
		}
		if (cfg.drawSlice == -1) SetInfo("Showing all slices");
		else SetInfo("Showing slice %d", cfg.drawSlice);
	}
	else if (wParam == 'J')
	{
		cfg.drawRelatedSlices ^= 1;
		SetInfo("Drawing of related slices %s", cfg.drawRelatedSlices ? "enabled" : "disabled");
	}
	else if (wParam == 'L')
	{
		if (cfg.showCollision >= nCollisions - 1)
		{
			cfg.showCollision = -1;
			SetInfo("Showing all collisions");
		}
		else
		{
			cfg.showCollision++;
			SetInfo("Showing collision %d", cfg.showCollision);
		}
	}
	else if (wParam == 'K')
	{
		if (cfg.showCollision <= -1)
		{
			cfg.showCollision = nCollisions - 1;
		}
		else
		{
			cfg.showCollision--;
		}
		if (cfg.showCollision == -1) SetInfo("Showing all collisions");
		else SetInfo("Showing collision %d", cfg.showCollision);
	}
	else if (wParam == 'F')
	{
		SwitchFullscreen();
		SetInfo("Toggling full screen");
	}
	else if (wParam == '_')
	{
		ToggleMaximized();
		SetInfo("Toggling maximized window");
	}
	else if (wParam == 'R')
	{
		maxFPSRate ^= 1;
		SetInfo("FPS rate %s", maxFPSRate ? "not limited" : "limited");
	}
	else if (wParam == 'H')
	{
		printInfoText += 1;
		printInfoText &= 3;
		SetInfo("Info text display - console: %s, onscreen %s", (printInfoText & 2) ? "enabled" : "disabled", (printInfoText & 1) ? "enabled" : "disabled");
	}
	else if (wParam == 'j')
	{
		separateGlobalTracks ^= 1;
		SetInfo("Seperated display of global tracks %s", separateGlobalTracks ? "enabled" : "disabled");
		updateDLList = true;
	}
	else if (wParam == 'c')
	{
		if (markClusters == 0) markClusters = 1;
		else if (markClusters >= 0x20) markClusters = 0;
		else markClusters <<= 1;
		SetInfo("Cluster flag highlight mask set to %d (%s)", markClusters, markClusters == 0 ? "off" : markClusters == 1 ? "split pad" : markClusters == 2 ? "split time" : markClusters == 4 ? "edge" : markClusters == 8 ? "singlePad" : markClusters == 0x10 ? "reject distance" : "reject error");
		updateDLList = true;
	}
	else if (wParam == 'B')
	{
		markAdjacentClusters++;
		if (markAdjacentClusters == 5) markAdjacentClusters = 7;
		if (markAdjacentClusters == 9) markAdjacentClusters = 15;
		if (markAdjacentClusters == 18) markAdjacentClusters = 0;
		if (markAdjacentClusters == 17) SetInfo("Marking protected clusters (%d)", markAdjacentClusters);
		else if (markAdjacentClusters == 16) SetInfo("Marking removable clusters (%d)", markAdjacentClusters);
		else SetInfo("Marking adjacent clusters (%d): rejected %s, tube %s, looper leg %s, low Pt %s", markAdjacentClusters, markAdjacentClusters & 1 ? "yes" : " no", markAdjacentClusters & 2 ? "yes" : " no", markAdjacentClusters & 4 ? "yes" : " no", markAdjacentClusters & 8 ? "yes" : " no");
		updateDLList = true;
	}
	else if (wParam == 'C')
	{
		cfg.colorCollisions ^= 1;
		SetInfo("Color coding of collisions %s", cfg.colorCollisions ? "enabled" : "disabled");
	}
	else if (wParam == 'V')
	{
		cfg.colorClusters ^= 1;
		SetInfo("Color coding for seed / trrack attachmend %s", cfg.colorClusters ? "enabled" : "disabled");
	}
	else if (wParam == 'E')
	{
		cfg.propagateTracks += 1;
		if (cfg.propagateTracks == 4) cfg.propagateTracks = 0;
		const char* infoText[] = {"Hits connected", "Hits connected and propagated to vertex", "Reconstructed track propagated inwards and outwards", "Monte Carlo track"};
		SetInfo("Display of propagated tracks: %s", infoText[cfg.propagateTracks]);
	}
	else if (wParam == 'G')
	{
		propagateLoopers ^= 1;
		SetInfo("Propagation of loopers %s", propagateLoopers ? "enabled" : "disabled");
		updateDLList = true;
	}
	else if (wParam == 'v')
	{
		hideRejectedClusters ^= 1;
		SetInfo("Rejected clusters are %s", hideRejectedClusters ? "hidden" : "shown");
		updateDLList = true;
	}
	else if (wParam == 'b')
	{
		hideUnmatchedClusters ^= 1;
		SetInfo("Unmatched clusters are %s", hideRejectedClusters ? "hidden" : "shown");
		updateDLList = true;
	}
	else if (wParam == 'i')
	{
		projectxy ^= 1;
		SetInfo("Projection onto xy plane %s", projectxy ? "enabled" : "disabled");
		updateDLList = true;
	}
	else if (wParam == 'S')
	{
		cfg.smoothPoints ^= true;
		SetInfo("Smoothing of points %s", cfg.smoothPoints ? "enabled" : "disabled");
	}
	else if (wParam == 'A')
	{
		cfg.smoothLines ^= true;
		SetInfo("Smoothing of lines %s", cfg.smoothLines ? "enabled" : "disabled");
	}
	else if (wParam == 'D')
	{
		cfg.depthBuffer ^= true;
		GLint depthBits;
		glGetIntegerv(GL_DEPTH_BITS, &depthBits);
		SetInfo("Depth buffer (z-buffer, %d bits) %s", depthBits, cfg.depthBuffer ? "enabled" : "disabled");
		setDepthBuffer();
	}
	else if (wParam == 'W')
	{
		drawQualityMSAA*= 2;
		if (drawQualityMSAA < 2) drawQualityMSAA = 2;
		if (drawQualityMSAA > 16) drawQualityMSAA = 0;
		UpdateOffscreenBuffers();
		SetInfo("Multisampling anti-aliasing factor set to %d", drawQualityMSAA);
	}
	else if (wParam == 'U')
	{
		drawQualityDownsampleFSAA++;
		if (drawQualityDownsampleFSAA == 1) drawQualityDownsampleFSAA = 2;
		if (drawQualityDownsampleFSAA == 5) drawQualityDownsampleFSAA = 0;
		UpdateOffscreenBuffers();
		SetInfo("Downsampling anti-aliasing factor set to %d", drawQualityDownsampleFSAA);
	}
	else if (wParam == 'V')
	{
		drawQualityVSync ^= true;
		SetVSync(drawQualityVSync);
		SetInfo("VSync: %s", drawQualityVSync ? "enabled" : "disabled");
	}
	else if (wParam == 'I')
	{
		useGLIndirectDraw ^= true;
		SetInfo("OpenGL Indirect Draw %s", useGLIndirectDraw ? "enabled" : "disabled");
		updateDLList = true;
	}
	else if (key == ';')
	{
		updateDLList = true;
		Xadd += 60;
		Zadd += 60;
		SetInfo("TPC sector separation: %f %f", Xadd, Zadd);
	}
	else if (key == ':')
	{
		updateDLList = true;
		Xadd -= 60;
		Zadd -= 60;
		if (Zadd < 0 || Xadd < 0) Zadd = Xadd = 0;
		SetInfo("TPC sector separation: %f %f", Xadd, Zadd);
	}
	else if (key == '#')
	{
		invertColors ^= 1;
		if (invertColors) {CHKERR(glClearColor(1.0f, 1.0f, 1.0f, 1.0f));}
		else {CHKERR(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));}
	}
	else if (wParam == 'g')
	{
		cfg.drawGrid ^= 1;
		SetInfo("Fast Cluster Search Grid %s", cfg.drawGrid ? "shown" : "hidden");
	}
	else if (wParam == 'x')
	{
		cfg.excludeClusters ^= 1;
		SetInfo(cfg.excludeClusters ? "Clusters of selected category are excluded from display" : "Clusters are shown");
	}
	else if (key == '.')
	{
		hideRejectedTracks ^= 1;
		SetInfo("Rejected tracks are %s", hideRejectedTracks ? "hidden" : "shown");
		updateDLList = true;
	}
	else if (wParam == '1')
	{
		cfg.drawClusters ^= 1;
	}
	else if (wParam == '2')
	{
		cfg.drawInitLinks ^= 1;
	}
	else if (wParam == '3')
	{
		cfg.drawLinks ^= 1;
	}
	else if (wParam == '4')
	{
		cfg.drawSeeds ^= 1;
	}
	else if (wParam == '5')
	{
		cfg.drawTracklets ^= 1;
	}
	else if (wParam == '6')
	{
		cfg.drawTracks ^= 1;
	}
	else if (wParam == '7')
	{
		cfg.drawGlobalTracks ^= 1;
	}
	else if (wParam == '8')
	{
		cfg.drawFinal ^= 1;
	}
	else if (wParam == 't')
	{
		printf("Taking screenshot\n");
		static int nScreenshot = 1;
		char fname[32];
		sprintf(fname, "screenshot%d.bmp", nScreenshot++);
		DoScreenshot(fname);
		SetInfo("Taking screenshot (%s)", fname);
	}
	else if (wParam == 'Z')
	{
		screenshot_scale += 1;
		if (screenshot_scale == 5) screenshot_scale = 1;
		SetInfo("Screenshot scaling factor set to %d", screenshot_scale);
	}
	else if (wParam == 'y' || wParam == 'T')
	{
		if ((animateScreenshot = wParam == 'T')) animationExport++;
		if (animateVectors[0].size() > 1)
		{
			startAnimation();
			SetInfo("Starting animation");
		}
		else
		{
			SetInfo("Insufficient animation points to start animation");
		}
	}
	else if (wParam == '>')
	{
		animationChangeConfig ^= 1;
		SetInfo("Interpolating visualization settings during animation %s", animationChangeConfig ? "enabled" : "disabled");
	}
	else if (wParam == 'Y')
	{
		setAnimationPoint();
		SetInfo("Added animation point (%d points, %6.2f seconds)", (int) animateVectors[0].size(), animateVectors[0].back());
	}
	else if (wParam == 'X')
	{
		resetAnimation();
		SetInfo("Reset animation points");
	}
	else if (wParam == '\'')
	{
		removeAnimationPoint();
		SetInfo("Removed animation point");
	}
	else if (wParam == 'M')
	{
		cfg.animationMode++;
		if (cfg.animationMode == 7) cfg.animationMode = 0;
		resetAnimation();
		if (cfg.animationMode == 6) SetInfo("Animation mode %d - Centered on origin", cfg.animationMode);
		else SetInfo("Animation mode %d - Position: %s, Direction: %s", cfg.animationMode, cfg.animationMode & 2 ? "Spherical (spherical rotation)" : cfg.animationMode & 4 ? "Spherical (Euler angles)" : "Cartesian", cfg.animationMode & 1 ? "Euler angles" : "Quaternion");
	}
	else if (wParam == 'o')
	{
		FILE *ftmp = fopen("glpos.tmp", "w+b");
		if (ftmp)
		{
			int retval = fwrite(&currentMatrix[0], sizeof(currentMatrix[0]), 16, ftmp);
			if (retval != 16) printf("Error writing position to file\n");
			else printf("Position stored to file\n");
			fclose(ftmp);
		}
		else
		{
			printf("Error opening file\n");
		}
		SetInfo("Camera position stored to file");
	}
	else if (wParam == 'p')
	{
		GLfloat tmp[16];
		FILE *ftmp = fopen("glpos.tmp", "rb");
		if (ftmp)
		{
			int retval = fread(&tmp[0], sizeof(tmp[0]), 16, ftmp);
			if (retval == 16)
			{
				glMatrixMode(GL_MODELVIEW);
				glLoadMatrixf(tmp);
				glGetFloatv(GL_MODELVIEW_MATRIX, currentMatrix);
				printf("Position read from file\n");
			}
			else
			{
				printf("Error reading position from file\n");
			}
			fclose(ftmp);
		}
		else
		{
			printf("Error opening file\n");
		}
		SetInfo("Camera position loaded from file");
	}
	else if (wParam == 'O')
	{
		FILE *ftmp = fopen("glanimation.tmp", "w+b");
		if (ftmp)
		{
			fwrite(&cfg, sizeof(cfg), 1, ftmp);
			int size = animateVectors[0].size();
			fwrite(&size, sizeof(size), 1, ftmp);
			for (int i = 0;i < 9;i++) fwrite(animateVectors[i].data(), sizeof(animateVectors[i][0]), size, ftmp);
			fwrite(animateConfig.data(), sizeof(animateConfig[0]), size, ftmp);
			fclose(ftmp);
		}
		else
		{
			printf("Error opening file\n");
		}
		SetInfo("Animation path stored to file");
	}
	else if (wParam == 'P')
	{
		FILE *ftmp = fopen("glanimation.tmp", "rb");
		if (ftmp)
		{
			int retval = fread(&cfg, sizeof(cfg), 1, ftmp);
			int size;
			retval += fread(&size, sizeof(size), 1, ftmp);
			for (int i = 0;i < 9;i++)
			{
				animateVectors[i].resize(size);
				retval += fread(animateVectors[i].data(), sizeof(animateVectors[i][0]), size, ftmp);
			}
			animateConfig.resize(size);
			retval += fread(animateConfig.data(), sizeof(animateConfig[0]), size, ftmp);
			fclose(ftmp);
			updateConfig();
		}
		else
		{
			printf("Error opening file\n");
		}
		SetInfo("Animation path loaded from file");
	}
	else if (wParam == 'h')
	{
		PrintHelp();
		SetInfo("Showing help text");
	}
	else if (key == '#')
	{
		testSetting++;
		SetInfo("Debug test variable set to %d", testSetting);
		updateDLList = true;
	}
}

void showInfo(const char* info)
{
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	gluOrtho2D(0.f, screen_width, 0.f, screen_height);
	glViewport(0, 0, screen_width, screen_height);
	float colorValue = invertColors ? 0.f : 1.f;
	if (invertColors) glColor3f(colorValue, colorValue, colorValue);
	else glColor3f(colorValue, colorValue, colorValue);
	glRasterPos2f(40.f, 40.f);
	OpenGLPrint(info);
	if (infoText2Timer.IsRunning())
	{
		if (infoText2Timer.GetCurrentElapsedTime() >= 6)
		{
			infoText2Timer.Reset();
		}
		else
		{
			if (infoText2Timer.GetCurrentElapsedTime() >= 5) glColor4f(colorValue, colorValue, colorValue, 6 - infoText2Timer.GetCurrentElapsedTime());
			glRasterPos2f(40.f, 20.f);
			OpenGLPrint(infoText2);
		}
	}
	if (infoHelpTimer.IsRunning())
	{
		if (infoHelpTimer.GetCurrentElapsedTime() >= 6)
		{
			infoHelpTimer.Reset();
		}
		else
		{
			if (infoHelpTimer.GetCurrentElapsedTime() >= 5) glColor4f(colorValue, colorValue, colorValue, 6 - infoHelpTimer.GetCurrentElapsedTime());
			for (unsigned int i = 0;i < sizeof(HelpText) / sizeof(HelpText[0]);i++)
			{
				glRasterPos2f(40.f, screen_height - 35 - 20 * (1 + i));
				OpenGLPrint(HelpText[i]);
			}
		}
	}
	glColor4f(colorValue, colorValue, colorValue, 0);
	glViewport(0, 0, render_width, render_height);
	glPopMatrix();
}

void HandleSendKey()
{
	if (sendKey)
	{
		//fprintf(stderr, "sendKey %d '%c'\n", sendKey, (char) sendKey);

		bool shifted = sendKey >= 'A' && sendKey <= 'Z';
		if (sendKey >= 'a' && sendKey <= 'z') sendKey ^= 'a' ^ 'A';
		bool oldShift = keysShift[sendKey];
		keysShift[sendKey] = shifted;
		HandleKeyRelease(sendKey, sendKey);
		keysShift[sendKey] = oldShift;
		sendKey = 0;
	}
}
