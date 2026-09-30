#include <iostream>
#include <cmath>

#include <Windows.h>
#include <windowsx.h>
#include <profileapi.h>

//sound wavelength should be around 8.5mm (at 40000Hz)
//transducer vibration plate radius = 6mm, diameter = 12mm

//grid in mm
//Hz in Hz(not kHz)
constexpr int simGridX = 150;
constexpr int simGridY = 150;
constexpr int transFreq = 40000;
constexpr int transRadi = 6;
constexpr int transDiam = 12;
constexpr float transNormV = 0.01f;

//air temperature = 21 C
constexpr double airPressure = 1007.383;
//density = kg/m3
constexpr double airDensity = 1.193063;
//per mm
constexpr double wavenumMM = 0.73273298042910629468516463749959;

constexpr double DEGTORAD = 0.017453292;
constexpr double RADTODEG = 57.2957795;
constexpr double TAU = 6.2831853071795864;
constexpr double PI = 3.1415926535897932;

//has alpha because DIB function(setDIBtoDevice) only supports 32 bits per pixel(one byte stride)
typedef struct {
	byte r, g, b, a;
} RGBA;

namespace mainWindow {
	//will write to renderPass and setdibits the bitmap over to provided hdc in once
	//hdc provided to render function IS the buffer bitmap
	//width and height ARE the resolution of the screen
	unsigned int w, h;
	RGBA* renderPass{};
	BITMAPINFO renderPassDIBINFO{};
}

void initBuffer(HWND hwnd, HDC& bufferHDC, HBITMAP& bufferHBM);
void freeBuffer(HDC& bufferHDC, HBITMAP& bufferHBM);
void printDebug(HDC hdc, int x, int y, COLORREF col, const wchar_t* format, ...);
LRESULT CALLBACK winProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

typedef double deg;
typedef double rad;

//transducer
typedef struct {
	unsigned int posX, posY;
	rad rot;
	bool inv;
} trans;

class sim {
private:
	float prsrField[simGridX][simGridY] = { 0 };
	trans* transArr;
	unsigned int transArrLen;
public:
	bool progress;
	float t;
	sim();
	void freeSim();
	void runPrsrSim();
	void renderPrsrField();
	void invertTrans(unsigned int transNum);
};

sim::sim() : progress(false), t(0) {
		rad theta = 0;
		transArr = new trans[16];
		transArrLen = 16;
		for (int i = 0; i < 16; i++) {
			transArr[i].posX = 75 + (unsigned int)(55.f * cos(theta));
			transArr[i].posY = 75 + (unsigned int)(55.f * sin(theta));
			transArr[i].rot = theta + PI / 2;
			transArr[i].inv = false;
			//2pi / 16
			theta += PI / 8;
		}
}

void sim::freeSim() {
	delete[] transArr;
}

//sim functions should scan through simgrid x, y
void sim::runPrsrSim() {
	//pressure field coordinates = x, y
	//surface coordinates = surfXY(determined from rovesurf)
	float surfX, surfY, dist, tx, ty, curSol;
	constexpr float waveCoefficient = (float)(transNormV * transFreq * airDensity);

	memset(prsrField, 0, sizeof(prsrField));
	for (unsigned int tarTrans = 0; tarTrans < sim::transArrLen; tarTrans++) {
		for (int x = 0; x < simGridX; x++) {
			for (int y = 0; y < simGridY; y++) {
				curSol = 0;
				for (int roveSurf = -transRadi; roveSurf < transRadi; roveSurf++) {
					surfX = (float)(cos(transArr[tarTrans].rot) * (double)roveSurf) + (float)transArr[tarTrans].posX;
					surfY = (float)(sin(transArr[tarTrans].rot) * (double)roveSurf) + (float)transArr[tarTrans].posY;
					tx = surfX - x;
					tx *= tx;
					ty = surfY - y;
					ty *= ty;
					dist = std::sqrt(tx + ty);
					curSol += (float)(sin(wavenumMM * dist) / (double)dist);
				}
				//i(cos(-kr) + i sin(-kr))
				//i(cos(-kr) - i sin(kr))
				//i cos(-kr) + sin(kr)
				//real = sin(kr)
				//transNormV multiplied outside since assuming constant surface normal velocity
				if(transArr[tarTrans].inv)
					curSol *= waveCoefficient * cos(t + (float)PI);
				else
					curSol *= waveCoefficient * cos(t);
				prsrField[x][y] += curSol;
			}
		}
	}
	return;
}

//render functions should scan through main window w, h along side the simgrid to prevent invalid access
void sim::renderPrsrField() {
	float eval, surfX, surfY;
	//background
	memset(mainWindow::renderPass, 10, mainWindow::renderPassDIBINFO.bmiHeader.biSizeImage);
	
	RGBA pixelCol{};
	for (int x = 0; x * 2 < (int)mainWindow::w && x < simGridX; x++) {
		for (int y = 0; y * 2 < (int)mainWindow::h && y < simGridY; y++) {
			/*
			val = sim::prsrField[x][y];
			if (val >= 0) {
				eval = log(val + 1);
			}
			else {
				eval = -log(-val + 1);
			}
			eval *= 10;
			eval += 128;
			*/
			eval = sim::prsrField[x][y];
			eval += 128;
			if (eval > 255)
				eval = 255;
			if (eval < 0)
				eval = 0;
			pixelCol.r = (char)eval;
			pixelCol.g = (char)eval;
			pixelCol.b = (char)eval;
			mainWindow::renderPass[(x * 2) + (y * 2) * mainWindow::w] = pixelCol;
			mainWindow::renderPass[(x * 2 + 1) + (y * 2) * mainWindow::w] = pixelCol;
			mainWindow::renderPass[(x * 2) + (y * 2 + 1) * mainWindow::w] = pixelCol;
			mainWindow::renderPass[(x * 2 + 1) + (y * 2 + 1) * mainWindow::w] = pixelCol;
		}
	}
	for (unsigned int tarTrans = 0; tarTrans < sim::transArrLen; tarTrans++) {
		if (sim::transArr[tarTrans].inv) {
			pixelCol.r = 250;
			pixelCol.g = 0;
			pixelCol.b = 0;
		}
		else {
			pixelCol.r = 0;
			pixelCol.g = 0;
			pixelCol.b = 250;
		}
		for (int roveSurf = -transRadi; roveSurf < transRadi; roveSurf++) {
			surfX = (float)(cos(sim::transArr[tarTrans].rot) * roveSurf + sim::transArr[tarTrans].posX);
			surfY = (float)(sin(sim::transArr[tarTrans].rot) * roveSurf + sim::transArr[tarTrans].posY);
			if ((unsigned int)surfX * 2 < mainWindow::w && (unsigned int)surfY * 2 < mainWindow::h) {
				mainWindow::renderPass[((unsigned int)surfX * 2) + ((unsigned int)surfY * 2) * mainWindow::w] = pixelCol;
				mainWindow::renderPass[((unsigned int)surfX * 2 + 1) + ((unsigned int)surfY * 2) * mainWindow::w] = pixelCol;
				mainWindow::renderPass[((unsigned int)surfX * 2) + ((unsigned int)surfY * 2 + 1) * mainWindow::w] = pixelCol;
				mainWindow::renderPass[((unsigned int)surfX * 2 + 1) + ((unsigned int)surfY * 2 + 1) * mainWindow::w] = pixelCol;
			}
		}
	}
	return;
}

/*
void printField(float(*inpPtr)[simGridY]) {
	for (int x = 0; x < simGridX; x++) {
		for (int y = 0; y < simGridY; y++) {
			std::cout << inpPtr[x][y] << ',';
		}
		std::cout << "\n\n\n";
	}
	return;
}
*/

void sim::invertTrans(unsigned int transNum) {
	if (transNum >= transArrLen) {
		//invalid transducer selected
		return;
	}
	if (sim::transArr[transNum].inv)
		sim::transArr[transNum].inv = false;
	else
		sim::transArr[transNum].inv = true;
	return;
}

namespace globals {
	bool quit = false, runsim = true;
	bool keyboard[256] = { 0 };
	long long fps = 0;
	int mouseX, mouseY;
	uint8_t mouseButton;

	unsigned int selected;

	sim mainSim;
}

enum { MOUSEL = 0b1, MOUSER = 0b10, MOUSEM = 0b100, MOUSEX1 = 0b1000, MOUSEX2 = 0b10000 };

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ PWSTR pCmdLine, _In_ int nCmdShow) {
	WNDCLASS wc = { };
	wc.lpfnWndProc = winProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = L"graphics main";
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	RegisterClass(&wc);

	HWND hwnd = CreateWindowEx(
		0, //optional window style
		L"graphics main", //window class name
		L"acoustic radiation pressure calc 2d", //window display text
		WS_OVERLAPPEDWINDOW, //window style
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, // position
		NULL, //parent window
		NULL, //menu
		hInstance, //instance handle
		NULL //additional data
	);
	if (hwnd == NULL) {
		return -1;
	}

	LARGE_INTEGER precisionClockFreq;
	LARGE_INTEGER precisionCurTime, precisionPrevTime;
	LONGLONG precisionFrameTime;

	QueryPerformanceFrequency(&precisionClockFreq);
	QueryPerformanceCounter(&precisionPrevTime);

	globals::selected = 0;
	ShowWindow(hwnd, nCmdShow);

	MSG msg = { 0 };
	int msgPerFrame;
	long double dt = 0.001, accum = 0;

	while (!globals::quit) {
		msgPerFrame = 0;
		while (PeekMessage(&msg, hwnd, 0, 0, PM_REMOVE)) {
			DispatchMessage(&msg);
			msgPerFrame++;
		}

		QueryPerformanceCounter(&precisionCurTime);
		precisionFrameTime = precisionCurTime.QuadPart - precisionPrevTime.QuadPart;
		precisionPrevTime = precisionCurTime;
		globals::fps = precisionClockFreq.QuadPart / precisionFrameTime;

		accum += (long double)precisionFrameTime / precisionClockFreq.QuadPart;

		while (accum >= dt) {
			//per 0.1 second
			if (globals::keyboard['W']) {
				//////////////////////////
			}
			if (globals::keyboard[VK_SPACE]) {
				globals::runsim = true;
			}
			if (globals::keyboard[VK_SHIFT]) {
				/////////////////////
			}
			if (globals::mainSim.progress) {
				globals::mainSim.t += (float)dt;
				globals::runsim = true;
			}
			accum -= dt;
		}
		InvalidateRect(hwnd, NULL, FALSE);
		Sleep(0);
	}
	globals::mainSim.freeSim();
	return 0;
}

LRESULT CALLBACK winProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	static HDC bufferHDC;
	static HBITMAP bufferHBM;

	static bool hasFocus = true;

	switch (uMsg) {
	case WM_CREATE: {
		ShowCursor(TRUE);
		initBuffer(hwnd, bufferHDC, bufferHBM);
		break;
	}
	case WM_MOVE:
	case WM_SIZE: {
		freeBuffer(bufferHDC, bufferHBM);
		initBuffer(hwnd, bufferHDC, bufferHBM);
		break;
	}
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);

		RECT clientRect;
		GetClientRect(hwnd, &clientRect);
		int width = clientRect.right - clientRect.left,
			height = clientRect.bottom - clientRect.top;

		if (globals::runsim) {
			globals::mainSim.runPrsrSim();
			globals::runsim = false;
		}
		//memset(mainWindow::renderPass, 10, mainWindow::renderPassDIBINFO.bmiHeader.biSizeImage);

		globals::mainSim.renderPrsrField();
		SetDIBitsToDevice(bufferHDC, 0, 0, mainWindow::w, mainWindow::h, 0, 0, 0, mainWindow::h, mainWindow::renderPass, &mainWindow::renderPassDIBINFO, DIB_RGB_COLORS);
		/*
		for (int x = 50; x < 100; x++) {
			for (int y = 50; y < 100; y++) {
				printDebug(bufferHDC, 50 * (x - 50), 15 * (y - 50), RGB(200, 200, 200), L"%.2f", sim::prsrField[x][y]);
			}
		}
		*/
		printDebug(bufferHDC, 10, 10, RGB(150, 150, 150), L"fps: %lld", globals::fps);
		printDebug(bufferHDC, -10, 10, RGB(150, 150, 150), L"[W] progress sim by 0.2s\n[R] pause/resume sim\n[T] select next transducer\n[G] invert phase\n[esc] quit");
		BitBlt(hdc, 0, 0, width, height, bufferHDC, 0, 0, SRCCOPY);
		EndPaint(hwnd, &ps);
		break;
	}
	case WM_KILLFOCUS:
		hasFocus = false;
		memset(globals::keyboard, 0, sizeof(globals::keyboard));
		globals::mouseButton = 0;
		break;
	case WM_SETFOCUS:
		hasFocus = true;
		break;
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
	case WM_KEYDOWN:
	case WM_KEYUP:
		if (hasFocus) {
			bool keyDownCur, keyDownPrev;
			keyDownPrev = ((lParam & ((uint32_t)1 << 30)) != 0);
			keyDownCur = ((lParam & ((uint32_t)1 << 31)) == 0);
			if (keyDownPrev != keyDownCur) {
				globals::keyboard[(uint8_t)wParam] = keyDownCur;
				//instant key processing
				if (keyDownCur) {
					switch (wParam) {
					case VK_ESCAPE:
						globals::quit = true;
						break;
					case 'W':
						globals::mainSim.t += 0.2f;
						globals::runsim = true;
						break;
					case 'R':
						if (globals::mainSim.progress)
							globals::mainSim.progress = false;
						else
							globals::mainSim.progress = true;
						break;
					case 'T':
						globals::selected++;
						if (globals::selected > 15)
							globals::selected = 0;
						break;
					case 'G':
						globals::mainSim.invertTrans(globals::selected);
						break;
					}
				}
			}
		}
		break;
	case WM_LBUTTONDOWN: globals::mouseButton |= MOUSEL; break;
	case WM_LBUTTONUP: globals::mouseButton &= ~MOUSEL; break;
	case WM_MBUTTONDOWN: globals::mouseButton |= MOUSEM; break;
	case WM_MBUTTONUP: globals::mouseButton &= ~MOUSEM; break;
	case WM_RBUTTONDOWN: globals::mouseButton |= MOUSER; break;
	case WM_RBUTTONUP: globals::mouseButton &= ~MOUSER; break;

	case WM_XBUTTONDOWN:
		if (GET_XBUTTON_WPARAM(wParam) == XBUTTON1)
			globals::mouseButton |= MOUSEX1;
		else
			globals::mouseButton |= MOUSEX2;
		break;

	case WM_XBUTTONUP:
		if (GET_XBUTTON_WPARAM(wParam) == XBUTTON1)
			globals::mouseButton &= ~MOUSEX1;
		else
			globals::mouseButton &= ~MOUSEX2;
		break;
		// did not implement WM_MOUSEWHEEL(scrolling)

	case WM_DESTROY:
		freeBuffer(bufferHDC, bufferHBM);
		globals::quit = true;
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
		break;
	}
	return 0;
}

//must alse be called when window or rendering area is resized
//for initialization call once, for resize call freeBuffer and call this
void initBuffer(HWND hwnd, HDC& bufferHDC, HBITMAP& bufferHBM) {
	RECT clientRect;
	GetClientRect(hwnd, &clientRect);
	//client rectangle is window coordinates, so left and top are always 0
	//right and bottoms are width and heights
	int w = clientRect.right, h = clientRect.bottom;

	HDC hdc = GetDC(hwnd);
	bufferHDC = CreateCompatibleDC(hdc);
	bufferHBM = CreateCompatibleBitmap(hdc, w, h);
	SelectObject(bufferHDC, bufferHBM);
	ReleaseDC(hwnd, hdc);

	mainWindow::w = w;
	mainWindow::h = h;

	int pixelCount = w * h;
	if (pixelCount)
		mainWindow::renderPass = new RGBA[pixelCount];

	mainWindow::renderPassDIBINFO.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	GetDIBits(bufferHDC, bufferHBM, 0, h, NULL, (LPBITMAPINFO)&mainWindow::renderPassDIBINFO, DIB_RGB_COLORS);

	BITMAPINFOHEADER& sceneBMINFO = mainWindow::renderPassDIBINFO.bmiHeader;

	sceneBMINFO.biWidth = w;
	sceneBMINFO.biHeight = -h; //top-down
	sceneBMINFO.biBitCount = 32;
	sceneBMINFO.biCompression = BI_RGB;
	sceneBMINFO.biSizeImage = pixelCount * 4;
	sceneBMINFO.biClrUsed = false;
	sceneBMINFO.biPlanes = 1;
	return;
}

void freeBuffer(HDC& bufferHDC, HBITMAP& bufferHBM) {
	DeleteObject(bufferHBM);
	DeleteDC(bufferHDC);
	if (mainWindow::renderPassDIBINFO.bmiHeader.biSizeImage)
		delete[] mainWindow::renderPass;
	return;
}

//10, 10 for x and y are default
//negative x or y means offset from right and bottom respectively
void printDebug(HDC hdc, int x, int y, COLORREF col, const wchar_t* format, ...) {
	va_list args;
	va_start(args, format);

	BITMAP structBitmapHeader;
	memset(&structBitmapHeader, 0, sizeof(BITMAP));
	HGDIOBJ hBitmap = GetCurrentObject(hdc, OBJ_BITMAP);
	GetObject(hBitmap, sizeof(BITMAP), &structBitmapHeader);

	int textPosX, textPosY;

	// bmWidth - (-x) = bmWidth + x
	if (x < 0)
		textPosX = structBitmapHeader.bmWidth + x;
	else
		textPosX = x;

	if (y < 0)
		textPosY = structBitmapHeader.bmHeight + y;
	else
		textPosY = y;

	RECT textBound = {
			textPosX,	//left
			textPosY,	//top
			textPosX,	//right
			textPosY,	//bottom
	};
	int prevBkMode = SetBkMode(hdc, TRANSPARENT), bufferSize = 128;
	COLORREF prevTextCol = SetTextColor(hdc, col);

	wchar_t* printBuffer = new wchar_t[bufferSize];
	vswprintf_s(printBuffer, bufferSize, format, args);
	if (x >= 0 && y >= 0) {
		//top left
		DrawText(hdc, printBuffer, -1, &textBound, DT_LEFT | DT_TOP | DT_NOCLIP);
	}
	else if (x < 0 && y >= 0) {
		//top right
		DrawText(hdc, printBuffer, -1, &textBound, DT_RIGHT | DT_TOP | DT_NOCLIP);
	}
	else if (x >= 0 && y < 0) {
		//bottom left
		DrawText(hdc, printBuffer, -1, &textBound, DT_LEFT | DT_BOTTOM | DT_NOCLIP);
	}
	else {
		//bottom right
		DrawText(hdc, printBuffer, -1, &textBound, DT_RIGHT | DT_BOTTOM | DT_NOCLIP);
	}
	delete[] printBuffer;

	va_end(args);
	SetBkMode(hdc, prevBkMode);
	SetTextColor(hdc, prevTextCol);
	return;
}