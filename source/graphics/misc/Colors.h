#pragma once

static const XMFLOAT4 CYAN = XMFLOAT4(0, 1, 1, 1);
static const XMFLOAT4 MAGENTA = XMFLOAT4(1, 0, 1, 1);
static const XMFLOAT4 YELLOW = XMFLOAT4(1, 1, 0, 1);
static const XMFLOAT4 RED = XMFLOAT4(1, 0, 0, 1);
static const XMFLOAT4 GREEN = XMFLOAT4(0, 1, 0, 1);
static const XMFLOAT4 BLUE = XMFLOAT4(0, 0, 1, 1);

static const XMFLOAT4 BLACK = XMFLOAT4(0, 0, 0, 1);
static const XMFLOAT4 WHITE = XMFLOAT4(1, 1, 1, 1);

static constexpr XMFLOAT4 axesRed = XMFLOAT4(226.0f / 256.0f, 117.0f / 256.0f, 117.0f / 256.0f, 0.5f);
static constexpr XMFLOAT4 axesGreen = XMFLOAT4(128.0f / 256.0f, 255.0f / 256.0f, 0.0f, 0.5f);
static constexpr XMFLOAT4 axesBlue = XMFLOAT4(45.0f / 256.0f, 117.0f / 256.0f, 191.0f / 256.0f, 0.5f);

namespace Colors {
	static float clearColor[4] = { 0,0,0,0 };
	static XMFLOAT4 SelectedColor = XMFLOAT4(0, 1, 0, 1);
}