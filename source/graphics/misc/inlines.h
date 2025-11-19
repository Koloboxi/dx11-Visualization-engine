#pragma once
#include <random>

inline void Subdivide(std::vector<Vertex>& vertices, std::vector<DWORD>& indices) {
	std::vector<DWORD> newIndices;
	newIndices.reserve(indices.size() * 4);

	for (size_t i = 0; i < indices.size(); i += 3) {
		DWORD i0 = indices[i];
		DWORD i1 = indices[i + 1];
		DWORD i2 = indices[i + 2];

		const Vertex& v0 = vertices[i0];
		const Vertex& v1 = vertices[i1];
		const Vertex& v2 = vertices[i2];

		Vertex m01, m12, m20;
		m01.pos.x = (v0.pos.x + v1.pos.x) * 0.5f;
		m01.pos.y = (v0.pos.y + v1.pos.y) * 0.5f;
		m01.pos.z = (v0.pos.z + v1.pos.z) * 0.5f;

		m12.pos.x = (v1.pos.x + v2.pos.x) * 0.5f;
		m12.pos.y = (v1.pos.y + v2.pos.y) * 0.5f;
		m12.pos.z = (v1.pos.z + v2.pos.z) * 0.5f;

		m20.pos.x = (v2.pos.x + v0.pos.x) * 0.5f;
		m20.pos.y = (v2.pos.y + v0.pos.y) * 0.5f;
		m20.pos.z = (v2.pos.z + v0.pos.z) * 0.5f;

		XMVECTOR posVec{};
		XMFLOAT3 posNorm{};

		posVec = XMLoadFloat3(&m01.pos);
		posVec = XMVector3Normalize(posVec);
		XMStoreFloat3(&posNorm, posVec);
		m01.normal = posNorm;

		posVec = XMLoadFloat3(&m12.pos);
		posVec = XMVector3Normalize(posVec);
		XMStoreFloat3(&posNorm, posVec);
		m12.normal = posNorm;

		posVec = XMLoadFloat3(&m20.pos);
		posVec = XMVector3Normalize(posVec);
		XMStoreFloat3(&posNorm, posVec);
		m20.normal = posNorm;


		vertices.push_back(m01);
		vertices.push_back(m12);
		vertices.push_back(m20);

		DWORD im01 = static_cast<DWORD>(vertices.size()) - 3;
		DWORD im12 = static_cast<DWORD>(vertices.size()) - 2;
		DWORD im20 = static_cast<DWORD>(vertices.size()) - 1;

		newIndices.push_back(i0);  newIndices.push_back(im01); newIndices.push_back(im20);
		newIndices.push_back(im01); newIndices.push_back(i1);   newIndices.push_back(im12);
		newIndices.push_back(im20); newIndices.push_back(im12); newIndices.push_back(i2);
		newIndices.push_back(im01); newIndices.push_back(im12); newIndices.push_back(im20);
	}

	indices = std::move(newIndices);
}

inline XMFLOAT4 FractionToRainbowColor(int numerator, int denominator) {
	if (denominator == 0) {
		return XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	float fraction = std::clamp(static_cast<float>(numerator) / denominator, 0.0f, 1.0f);

	float h = (1.0f - fraction) * 0.6667f; 
	float s = 1.0f;
	float l = 0.5f;

	float r, g, b;
	if (s == 0) {
		r = g = b = l;
	}
	else {
		auto hue2rgb = [](float p, float q, float t) {
			if (t < 0) t += 1;
			if (t > 1) t -= 1;
			if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
			if (t < 0.5f) return q;
			if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
			return p;
			};

		float q = l < 0.5f ? l * (1 + s) : l + s - l * s;
		float p = 2 * l - q;
		r = hue2rgb(p, q, h + 1.0f / 3.0f);
		g = hue2rgb(p, q, h);
		b = hue2rgb(p, q, h - 1.0f / 3.0f);
	}

	return XMFLOAT4(r, g, b, 1.0f);
}

inline XMFLOAT3 GenerateRandomFloat3(int limit) {
	static std::random_device rd;
	static std::mt19937 gen(rd());
	std::uniform_real_distribution<float> dist(-static_cast<float>(limit),
		static_cast<float>(limit));

	return XMFLOAT3{
		dist(gen),
		dist(gen),
		dist(gen)
	};
}

inline XMFLOAT4 GenerateRandomFloat4(int limit) {
	XMFLOAT3 col = GenerateRandomFloat3(limit);

	return XMFLOAT4{
		col.x,
		col.y,
		col.z,
		1
	};
}

inline std::vector<float> GetAllFloatsInRange() {
	std::vector<float> result;

	float f = 1.0f;

	for (WORD i = 0; i < MAXWORD; ++i) {
		result.push_back(f);

		UINT f_cast = *(UINT*)&f;
		f_cast &= 0xffff0000;

		f_cast |= i;

		f = *(float*)&f_cast;
	}

	return result;
}