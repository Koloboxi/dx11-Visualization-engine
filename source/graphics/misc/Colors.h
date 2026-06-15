#pragma once

static constexpr XMFLOAT4 axesRed = XMFLOAT4(226.0f / 256.0f, 117.0f / 256.0f, 117.0f / 256.0f, 0.5f);
static constexpr XMFLOAT4 axesGreen = XMFLOAT4(128.0f / 256.0f, 255.0f / 256.0f, 0.0f, 0.5f);
static constexpr XMFLOAT4 axesBlue = XMFLOAT4(45.0f / 256.0f, 117.0f / 256.0f, 191.0f / 256.0f, 0.5f);

namespace Colors {
	static float clearColor[4] = { 0,0,0,0 };
	static XMFLOAT4 SelectedColor = XMFLOAT4(0, 1, 0, 1);

	static const XMFLOAT4 BLACK = XMFLOAT4(0, 0, 0, 1);
	static const XMFLOAT4 WHITE = XMFLOAT4(1, 1, 1, 1);

	static const XMFLOAT4 GRAY = XMFLOAT4(0.5f, 0.5f, 0.5f, 0.5f);

	static const XMFLOAT4 RED = XMFLOAT4(1, 0, 0, 1);
	static const XMFLOAT4 GREEN = XMFLOAT4(0, 1, 0, 1);
	static const XMFLOAT4 BLUE = XMFLOAT4(0, 0, 1, 1);

	static const XMFLOAT4 CYAN = XMFLOAT4(0, 1, 1, 1);
	static const XMFLOAT4 MAGENTA = XMFLOAT4(1, 0, 1, 1);
	static const XMFLOAT4 YELLOW = XMFLOAT4(1, 1, 0, 1);

	static const XMFLOAT4 FRONT_FACE_WHITE = XMFLOAT4(0.94f, 0.93f, 0.91f, 1.0f);
	static const XMFLOAT4 BACK_FACE_RED    = XMFLOAT4(0.81f, 0.20f, 0.16f, 1.0f);

	// Linear interpolation between two colours by parameter T.
	// T is clamped to [0,1]: T=0 returns 'a', T=1 returns 'b'.
	// 'inline' (not 'static'): a single shared definition is emitted across
	// every translation unit that includes this header, so all callers see
	// the same function with external linkage. 'static' would instead give
	// each TU its own private copy (code bloat and a different address per TU);
	// a plain non-inline definition in a header would break the ODR with
	// "multiple definition" link errors. 'inline' is the idiomatic choice for
	// a small header-only helper.
	inline XMFLOAT4 Lerp(const XMFLOAT4& a, const XMFLOAT4& b, float T) {
		if (T < 0.0f) T = 0.0f;
		if (T > 1.0f) T = 1.0f;
		return XMFLOAT4(
			a.x + (b.x - a.x) * T,
			a.y + (b.y - a.y) * T,
			a.z + (b.z - a.z) * T,
			a.w + (b.w - a.w) * T);
	}
}