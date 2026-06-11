#include "primitive.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <tuple>

namespace PrimitiveConstructor
{
	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* deviceContext = nullptr;
}

Primitive* PrimitiveConstructor::Point(const XMFLOAT3& pos, const XMFLOAT4& col, UINT id)
{
	Primitive* point = new Primitive(device, deviceContext);

	Vertex vertexData[1] = { Vertex(BaseVectors::ORIGIN) };

	point->SetVertexIndexBuffers(vertexData, 1, 0, 0, 0);
	point->SetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_POINTLIST);
	point->SetPosition(pos);
	point->SetColor(col);
	point->SetIlluminationCapability(false);
	point->id = static_cast<UINT>(id);

	return point;
}

Primitive* PrimitiveConstructor::Line(const std::vector<XMFLOAT3>& poses, const XMFLOAT4& col, UINT id)
{
	Primitive* line = new Primitive(device, deviceContext);

	std::vector<Vertex> vertexData{};
	for (XMFLOAT3 pos : poses) {
		vertexData.push_back(Vertex(pos));
	}

	line->SetVertexIndexBuffers(vertexData.data(), vertexData.size(), 0, 0, 1);
	line->SetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_LINESTRIP);
	line->SetPosition(BaseVectors::ORIGIN);
	line->SetColor(col);
	line->SetIlluminationCapability(false);
	line->id = static_cast<UINT>(id);

	return line;
}

Primitive* PrimitiveConstructor::ColoredLine(const std::vector<XMFLOAT3>& poses, const std::vector<XMFLOAT4>& cols, bool asLineList, UINT id)
{
	Primitive* line = new Primitive(device, deviceContext);

	std::vector<Vertex> vertexData{};
	vertexData.reserve(poses.size());
	for (size_t i = 0; i < poses.size(); ++i) {
		XMFLOAT4 c = (i < cols.size()) ? cols[i] : XMFLOAT4(1, 1, 1, 1);
		vertexData.push_back(Vertex(poses[i], c));
	}

	line->SetVertexIndexBuffers(vertexData.data(), static_cast<UINT>(vertexData.size()), 0, 0, 1);
	line->SetPrimitiveTopology(asLineList ? D3D10_PRIMITIVE_TOPOLOGY_LINELIST : D3D10_PRIMITIVE_TOPOLOGY_LINESTRIP);
	line->SetPosition(BaseVectors::ORIGIN);
	line->SetColor(XMFLOAT4(1, 1, 1, 1));
	line->SetIlluminationCapability(false);
	line->SetUseVertexColor(true);
	line->id = static_cast<UINT>(id);

	return line;
}

Primitive* PrimitiveConstructor::ColoredTriangles(const std::vector<XMFLOAT3>& poses, const std::vector<XMFLOAT4>& cols, UINT id, bool ensureCCW)
{
	Primitive* tri = new Primitive(device, deviceContext);

	std::vector<Vertex> vertexData;
	vertexData.reserve(poses.size());

	const size_t triCount = poses.size() / 3;

	// Per-triangle face normal (with the current winding) and a flag telling us
	// to reverse the winding when emitting vertices.
	std::vector<XMFLOAT3> normals(triCount);
	std::vector<char> flip(triCount, 0);
	for (size_t t = 0; t < triCount; ++t)
		normals[t] = math::ComputeNormal(poses[t * 3 + 0], poses[t * 3 + 1], poses[t * 3 + 2]);

	// Orientation is made consistent not against a global centroid but locally:
	// any two triangles sharing an edge must have a positive dot product of
	// their normals. We flood-fill across the shared-edge adjacency graph and
	// flip a neighbour whenever its normal disagrees with the already-fixed one.
	// This keeps the flat-shaded surface lit on a single coherent side even for
	// non-convex geometry, where the centroid test would fail. The first
	// triangle of each connected component keeps its incoming winding as the
	// reference. Disabled (ensureCCW == false) for the untouched source surface.
	if (ensureCCW && triCount > 1) {
		// Weld coincident vertices so triangles that share an edge are detected
		// even though each triangle carries its own expanded copies. Positions
		// are quantised to a fine grid to tolerate float round-off.
		auto key = [](const XMFLOAT3& p) {
			const float q = 1e5f;
			return std::make_tuple((long long)std::lround(p.x * q),
			                       (long long)std::lround(p.y * q),
			                       (long long)std::lround(p.z * q));
		};
		std::map<std::tuple<long long, long long, long long>, int> vertId;
		std::vector<int> canon(poses.size());
		for (size_t i = 0; i < poses.size(); ++i) {
			auto k = key(poses[i]);
			auto it = vertId.find(k);
			if (it == vertId.end()) it = vertId.emplace(k, (int)vertId.size()).first;
			canon[i] = it->second;
		}

		// Map each undirected edge (canonical vertex pair) to the triangles that
		// use it, then connect triangles sharing an edge.
		std::map<std::pair<int, int>, std::vector<int>> edgeTris;
		auto edgeKey = [](int a, int b) { return a < b ? std::make_pair(a, b) : std::make_pair(b, a); };
		for (size_t t = 0; t < triCount; ++t) {
			int v0 = canon[t * 3 + 0], v1 = canon[t * 3 + 1], v2 = canon[t * 3 + 2];
			edgeTris[edgeKey(v0, v1)].push_back((int)t);
			edgeTris[edgeKey(v1, v2)].push_back((int)t);
			edgeTris[edgeKey(v2, v0)].push_back((int)t);
		}
		std::vector<std::vector<int>> adj(triCount);
		for (const auto& e : edgeTris) {
			const auto& ts = e.second;
			for (size_t i = 0; i < ts.size(); ++i)
				for (size_t j = i + 1; j < ts.size(); ++j) {
					adj[ts[i]].push_back(ts[j]);
					adj[ts[j]].push_back(ts[i]);
				}
		}

		std::vector<char> visited(triCount, 0);
		for (size_t s = 0; s < triCount; ++s) {
			if (visited[s]) continue;
			visited[s] = 1;
			std::queue<int> q;
			q.push((int)s);
			while (!q.empty()) {
				int t = q.front(); q.pop();
				const XMFLOAT3& nt = normals[t];
				for (int nb : adj[t]) {
					if (visited[nb]) continue;
					visited[nb] = 1;
					const XMFLOAT3& nn = normals[nb];
					if (nt.x * nn.x + nt.y * nn.y + nt.z * nn.z < 0.0f) {
						flip[nb] = 1;
						normals[nb] = XMFLOAT3(-nn.x, -nn.y, -nn.z);
					}
					q.push(nb);
				}
			}
		}
	}

	for (size_t t = 0; t < triCount; ++t) {
		// Local copies so we can swap two corners (and their colours together) to
		// reverse the winding when this triangle was flagged for flipping.
		size_t i0 = t * 3 + 0, i1 = t * 3 + 1, i2 = t * 3 + 2;
		if (flip[t]) std::swap(i1, i2);
		const XMFLOAT3& n = normals[t];

		const size_t idx[3] = { i0, i1, i2 };
		for (int k = 0; k < 3; ++k) {
			Vertex v(poses[idx[k]], n);
			v.color = (idx[k] < cols.size()) ? cols[idx[k]] : XMFLOAT4(1, 1, 1, 1);
			vertexData.push_back(v);
		}
	}

	tri->SetVertexIndexBuffers(vertexData.data(), static_cast<UINT>(vertexData.size()), nullptr, 0, 2);
	tri->SetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	tri->SetPosition(BaseVectors::ORIGIN);
	tri->SetColor(XMFLOAT4(1, 1, 1, 1));
	tri->SetIlluminationCapability(true);
	tri->SetUseVertexColor(true);
	tri->id = static_cast<UINT>(id);

	return tri;
}

Primitive* PrimitiveConstructor::Polygon(const std::vector<XMFLOAT3>& poses, const XMFLOAT4& col, UINT id)
{
	Primitive* poly = new Primitive(device, deviceContext);
	std::vector<Vertex> vertexData{};

	if (poses.size() == 3) {
		XMFLOAT3 n = math::ComputeNormal(poses[0], poses[1], poses[2]);
		vertexData = { Vertex(poses[0], n), Vertex(poses[1], n), Vertex(poses[2], n) };
		DWORD idx[3] = { 0, 1, 2 };
		poly->SetVertexIndexBuffers(vertexData.data(), 3, idx, 3, 2);
		poly->SetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}
	else {
		DWORD centerIndex = vertexData.size();
		XMFLOAT3 centerOfMass = math::GetCenterOfMass(poses);

		for (int i = 0; i < poses.size(); ++i) {
			XMFLOAT3 faceNormal = math::ComputeNormal(centerOfMass, poses[i], poses[(i + 1) % poses.size()]);

			vertexData.push_back(Vertex(centerOfMass, faceNormal));
			vertexData.push_back(Vertex(poses[i], faceNormal));
			vertexData.push_back(Vertex(poses[(i + 1) % poses.size()], faceNormal));
		}

		std::vector<DWORD> indexData{};
		for (DWORD i = 0; i < vertexData.size(); ++i) {
			indexData.push_back(i);
		}
		poly->SetVertexIndexBuffers(vertexData.data(), vertexData.size(), indexData.data(), indexData.size(), 2);
		poly->SetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}

	poly->SetPosition(BaseVectors::ORIGIN);
	poly->SetColor(col);
	poly->SetIlluminationCapability(true);
	poly->id = static_cast<UINT>(id);

	return poly;
}

Primitive* PrimitiveConstructor::Sphere(float radius, const XMFLOAT3& pos, const UINT numSubdivides, const XMFLOAT4& col, UINT id)
{
	std::vector<Vertex> vertexData = {
	{ {  0.0f,      0.0f,      1.0f     }, {  0.0f,      0.0f,      1.0f     } },

	{ {  0.894427f, 0.0f,      0.447214f}, {  0.894427f, 0.0f,      0.447214f} },
	{ {  0.276393f, 0.850651f, 0.447214f}, {  0.276393f, 0.850651f, 0.447214f} },
	{ { -0.723607f, 0.525731f, 0.447214f}, { -0.723607f, 0.525731f, 0.447214f} },
	{ { -0.723607f,-0.525731f, 0.447214f}, { -0.723607f,-0.525731f, 0.447214f} },
	{ {  0.276393f,-0.850651f, 0.447214f}, {  0.276393f,-0.850651f, 0.447214f} },

	{ {  0.723607f, 0.525731f,-0.447214f}, {  0.723607f, 0.525731f,-0.447214f} },
	{ { -0.276393f, 0.850651f,-0.447214f}, { -0.276393f, 0.850651f,-0.447214f} },
	{ { -0.894427f, 0.0f,     -0.447214f}, { -0.894427f, 0.0f,     -0.447214f} },
	{ { -0.276393f,-0.850651f,-0.447214f}, { -0.276393f,-0.850651f,-0.447214f} },
	{ {  0.723607f,-0.525731f,-0.447214f}, {  0.723607f,-0.525731f,-0.447214f} },

	{ {  0.0f,      0.0f,     -1.0f     }, {  0.0f,      0.0f,     -1.0f     } }
	};

	std::vector<DWORD> indexData = {
		0, 1, 2,
		0, 2, 3,
		0, 3, 4,
		0, 4, 5,
		0, 5, 1,

		1, 6, 2,
		2, 6, 7,
		2, 7, 3,
		3, 7, 8,
		3, 8, 4,
		4, 8, 9,
		4, 9, 5,
		5, 9, 10,
		5, 10, 1,
		1, 10, 6,

		11, 7, 6,
		11, 8, 7,
		11, 9, 8,
		11, 10, 9,
		11, 6, 10
	};

	for (UINT i = 0; i < numSubdivides; ++i) {
		Subdivide(vertexData, indexData);
	}

	for (Vertex& vertex : vertexData) {
		XMVECTOR posVec = XMLoadFloat3(&vertex.pos);
		posVec = XMVector3Normalize(posVec);
		XMStoreFloat3(&vertex.pos, posVec);

		vertex.pos.x *= radius;
		vertex.pos.y *= radius;
		vertex.pos.z *= radius;
	}

	Primitive* poly = new Primitive(device, deviceContext);
	poly->SetVertexIndexBuffers(vertexData.data(), vertexData.size(), indexData.data(), indexData.size(), 2);
	poly->SetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	poly->SetPosition(pos);
	poly->SetColor(col);
	poly->SetIlluminationCapability(true);
	poly->id = static_cast<UINT>(id);

	return poly;
}

Primitive* PrimitiveConstructor::Line3d(float radius, const std::vector<XMFLOAT3>& poses, const UINT numSubdivides, const XMFLOAT4& col, UINT id)
{
	std::vector<Vertex> vertexData;
	std::vector<DWORD>  indexData;

	const UINT N = (numSubdivides < 3) ? 3 : numSubdivides;
	if (poses.size() < 2) return nullptr;

	auto XM = [](const XMFLOAT3& p) { return XMLoadFloat3(&p); };

	UINT ringStride = N;
	UINT ringCount = poses.size();

	for (size_t i = 0; i < poses.size(); i++)
	{
		XMVECTOR p = XM(poses[i]);

		XMVECTOR dir;
		if (i + 1 < poses.size()) dir = XM(poses[i + 1]) - p;
		else                      dir = p - XM(poses[i - 1]);
		dir = XMVector3Normalize(dir);


		XMVECTOR up = XMVectorSet(0, 1, 0, 0);
		if (fabs(XMVectorGetX(XMVector3Dot(up, dir))) > 0.99f)
			up = XMVectorSet(1, 0, 0, 0);

		XMVECTOR xAxis = XMVector3Normalize(XMVector3Cross(up, dir));
		XMVECTOR yAxis = XMVector3Normalize(XMVector3Cross(dir, xAxis));

		for (UINT k = 0; k < N; k++)
		{
			float a = (float)k / (float)N * XM_2PI;
			float ca = cosf(a), sa = sinf(a);

			XMVECTOR offset =
				xAxis * (ca * radius) +
				yAxis * (sa * radius);

			XMVECTOR pos = p + offset;
			XMFLOAT3 pos3; XMStoreFloat3(&pos3, pos);

			XMFLOAT3 n3;
			XMStoreFloat3(&n3, XMVector3Normalize(offset));

			vertexData.push_back({ pos3, n3 });
		}
	}

	for (UINT r = 0; r + 1 < ringCount; r++)
	{
		UINT base0 = r * ringStride;
		UINT base1 = (r + 1) * ringStride;

		for (UINT k = 0; k < N; k++)
		{
			UINT k0 = k;
			UINT k1 = (k + 1) % N;

			UINT v00 = base0 + k0;
			UINT v01 = base0 + k1;
			UINT v10 = base1 + k0;
			UINT v11 = base1 + k1;

			indexData.push_back(v00);
			indexData.push_back(v01);
			indexData.push_back(v10);

			indexData.push_back(v01);
			indexData.push_back(v11);
			indexData.push_back(v10);
		}
	}

	{
		XMFLOAT3 c = poses.front();

		XMVECTOR dir = XM(poses[1]) - XM(poses[0]);
		dir = XMVector3Normalize(dir);
		XMVECTOR nvec = -dir;
		XMFLOAT3 n; XMStoreFloat3(&n, nvec);

		UINT centerIndex = (UINT)vertexData.size();
		vertexData.push_back({ c, n });

		UINT base = 0;

		for (UINT k = 0; k < N; k++)
		{
			UINT k0 = k;
			UINT k1 = (k + 1) % N;

			indexData.push_back(centerIndex);
			indexData.push_back(base + k1);
			indexData.push_back(base + k0);
		}
	}

	{
		XMFLOAT3 c = poses.back();

		XMVECTOR dir = XM(poses.back()) - XM(poses[poses.size() - 2]);
		dir = XMVector3Normalize(dir);
		XMVECTOR nvec = dir;
		XMFLOAT3 n; XMStoreFloat3(&n, nvec);

		UINT centerIndex = (UINT)vertexData.size();
		vertexData.push_back({ c, n });

		UINT base = (poses.size() - 1) * N;

		for (UINT k = 0; k < N; k++)
		{
			UINT k0 = k;
			UINT k1 = (k + 1) % N;

			indexData.push_back(centerIndex);
			indexData.push_back(base + k0);
			indexData.push_back(base + k1);
		}
	}

	Primitive* poly = new Primitive(device, deviceContext);
	poly->SetVertexIndexBuffers(
		vertexData.data(), vertexData.size(),
		indexData.data(), indexData.size(), 2);

	poly->SetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	poly->SetPosition({ 0,0,0 });
	poly->SetColor(col);
	poly->SetIlluminationCapability(true);
	poly->id = id;

	return poly;
}

Primitive* PrimitiveConstructor::Arc3d(float arcRadius, float lineRadius, float angleDeg, const XMFLOAT3& center, const UINT numSubdivides, const XMFLOAT4& col, UINT id)
{
	if (!arcRadius || !lineRadius || !angleDeg) return nullptr;
	float angleRad = XM_PI * angleDeg / 180.f;

	std::vector<XMFLOAT3> poses{};
	for (UINT i = 0; i <= numSubdivides; ++i) {
		float t = angleRad * i / numSubdivides;
		XMFLOAT3 p = BaseVectors::YVEC * arcRadius * cos(t) + BaseVectors::XVEC * arcRadius * sin(t);
		poses.push_back(p);
	}

	return Line3d(lineRadius, poses, 5, col, id);
}

Primitive* PrimitiveConstructor::CubeWireframe(float halfSize, const XMFLOAT3& center, const XMFLOAT4& col, UINT id)
{
	float L = halfSize;
	float cx = center.x, cy = center.y, cz = center.z;

	XMFLOAT3 corners[8] = {
		{cx-L, cy-L, cz-L},
		{cx+L, cy-L, cz-L},
		{cx+L, cy+L, cz-L},
		{cx-L, cy+L, cz-L},
		{cx-L, cy-L, cz+L},
		{cx+L, cy-L, cz+L},
		{cx+L, cy+L, cz+L},
		{cx-L, cy+L, cz+L},
	};

	int edgeIdx[24] = {
		0,1, 1,2, 2,3, 3,0,
		4,5, 5,6, 6,7, 7,4,
		0,4, 1,5, 2,6, 3,7
	};
	std::vector<Vertex> verts;
	verts.reserve(24);
	for (int k = 0; k < 24; k++)
		verts.push_back(Vertex(corners[edgeIdx[k]]));

	Primitive* p = new Primitive(device, deviceContext);
	p->SetVertexIndexBuffers(verts.data(), 24, nullptr, 0, 1);
	p->SetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_LINELIST);
	p->SetPosition({0, 0, 0});
	p->SetColor(col);
	p->SetIlluminationCapability(false);
	p->id = id;
	return p;
}

Primitive* PrimitiveConstructor::CubeSolid(float halfSize, const XMFLOAT3& center, const XMFLOAT4& col, UINT id)
{
	float L = halfSize;
	float cx = center.x, cy = center.y, cz = center.z;

	auto v = [](float x, float y, float z, float nx, float ny, float nz) -> Vertex {
		return Vertex({x, y, z}, {nx, ny, nz});
	};

	std::vector<Vertex> verts = {
		v(cx+L, cy-L, cz-L,  1,0,0), v(cx+L, cy+L, cz-L,  1,0,0), v(cx+L, cy+L, cz+L,  1,0,0),
		v(cx+L, cy-L, cz-L,  1,0,0), v(cx+L, cy+L, cz+L,  1,0,0), v(cx+L, cy-L, cz+L,  1,0,0),
		v(cx-L, cy-L, cz+L, -1,0,0), v(cx-L, cy+L, cz+L, -1,0,0), v(cx-L, cy+L, cz-L, -1,0,0),
		v(cx-L, cy-L, cz+L, -1,0,0), v(cx-L, cy+L, cz-L, -1,0,0), v(cx-L, cy-L, cz-L, -1,0,0),
		v(cx-L, cy+L, cz-L,  0,1,0), v(cx-L, cy+L, cz+L,  0,1,0), v(cx+L, cy+L, cz+L,  0,1,0),
		v(cx-L, cy+L, cz-L,  0,1,0), v(cx+L, cy+L, cz+L,  0,1,0), v(cx+L, cy+L, cz-L,  0,1,0),
		v(cx-L, cy-L, cz+L,  0,-1,0), v(cx-L, cy-L, cz-L,  0,-1,0), v(cx+L, cy-L, cz-L,  0,-1,0),
		v(cx-L, cy-L, cz+L,  0,-1,0), v(cx+L, cy-L, cz-L,  0,-1,0), v(cx+L, cy-L, cz+L,  0,-1,0),
		v(cx-L, cy-L, cz+L,  0,0,1), v(cx+L, cy-L, cz+L,  0,0,1), v(cx+L, cy+L, cz+L,  0,0,1),
		v(cx-L, cy-L, cz+L,  0,0,1), v(cx+L, cy+L, cz+L,  0,0,1), v(cx-L, cy+L, cz+L,  0,0,1),
		v(cx+L, cy-L, cz-L,  0,0,-1), v(cx-L, cy-L, cz-L,  0,0,-1), v(cx-L, cy+L, cz-L,  0,0,-1),
		v(cx+L, cy-L, cz-L,  0,0,-1), v(cx-L, cy+L, cz-L,  0,0,-1), v(cx+L, cy+L, cz-L,  0,0,-1),
	};

	Primitive* p = new Primitive(device, deviceContext);
	p->SetVertexIndexBuffers(verts.data(), (UINT)verts.size(), nullptr, 0, 2);
	p->SetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	p->SetPosition({0, 0, 0});
	p->SetColor(col);
	p->SetIlluminationCapability(true);
	p->id = id;
	return p;
}

Primitive* PrimitiveConstructor::Arrow3d(
	float shaftRadius, float headRadius, float headLength,
	const XMFLOAT3& fromF, const XMFLOAT3& toF,
	UINT sides, const XMFLOAT4& col, UINT id)
{
	if (sides < 3) sides = 3;
	const UINT N = sides;

	auto XM = [](const XMFLOAT3& p) { return XMLoadFloat3(&p); };
	XMVECTOR from = XM(fromF);
	XMVECTOR to   = XM(toF);
	XMVECTOR diff = to - from;
	float totalLen = XMVectorGetX(XMVector3Length(diff));
	if (totalLen < 1e-5f) return nullptr;

	XMVECTOR dir = diff / totalLen;

	float shaftLen = totalLen - headLength;
	if (shaftLen < 0.0f) shaftLen = 0.0f;
	XMVECTOR shaftEnd = from + dir * shaftLen;

	XMVECTOR up = XMVectorSet(0, 1, 0, 0);
	if (fabsf(XMVectorGetX(XMVector3Dot(up, dir))) > 0.99f)
		up = XMVectorSet(1, 0, 0, 0);
	XMVECTOR xAxis = XMVector3Normalize(XMVector3Cross(up, dir));
	XMVECTOR yAxis = XMVector3Normalize(XMVector3Cross(dir, xAxis));

	std::vector<Vertex> verts;
	std::vector<DWORD>  idx;
	verts.reserve(N * 6 + 4);
	idx.reserve(N * 12);

	auto storeV = [](XMVECTOR p, XMVECTOR n) -> Vertex {
		XMFLOAT3 pf, nf;
		XMStoreFloat3(&pf, p); XMStoreFloat3(&nf, n);
		return { pf, nf };
	};

	if (shaftLen > 1e-5f) {
		UINT ring0 = (UINT)verts.size();
		for (UINT k = 0; k < N; k++) {
			float a = (float)k / N * XM_2PI;
			XMVECTOR r = xAxis * cosf(a) + yAxis * sinf(a);
			verts.push_back(storeV(from + r * shaftRadius, r));
		}
		UINT ring1 = (UINT)verts.size();
		for (UINT k = 0; k < N; k++) {
			float a = (float)k / N * XM_2PI;
			XMVECTOR r = xAxis * cosf(a) + yAxis * sinf(a);
			verts.push_back(storeV(shaftEnd + r * shaftRadius, r));
		}
		for (UINT k = 0; k < N; k++) {
			UINT k1 = (k + 1) % N;
			idx.push_back(ring0 + k);  idx.push_back(ring0 + k1); idx.push_back(ring1 + k);
			idx.push_back(ring0 + k1); idx.push_back(ring1 + k1); idx.push_back(ring1 + k);
		}
		UINT bc = (UINT)verts.size();
		verts.push_back(storeV(from, -dir));
		for (UINT k = 0; k < N; k++) {
			UINT k1 = (k + 1) % N;
			idx.push_back(bc); idx.push_back(ring0 + k1); idx.push_back(ring0 + k);
		}
	}

	UINT coneRing = (UINT)verts.size();
	for (UINT k = 0; k < N; k++) {
		float a = (float)k / N * XM_2PI;
		XMVECTOR r = xAxis * cosf(a) + yAxis * sinf(a);
		XMVECTOR n = XMVector3Normalize(r * headRadius - dir * headLength);
		verts.push_back(storeV(shaftEnd + r * headRadius, n));
	}
	UINT apexBase = (UINT)verts.size();
	XMFLOAT3 tipPos; XMStoreFloat3(&tipPos, to);
	for (UINT k = 0; k < N; k++) {
		float a = ((float)k + 0.5f) / N * XM_2PI;
		XMVECTOR r = xAxis * cosf(a) + yAxis * sinf(a);
		XMVECTOR n = XMVector3Normalize(r * headRadius - dir * headLength);
		XMFLOAT3 nf; XMStoreFloat3(&nf, n);
		verts.push_back({ tipPos, nf });
	}
	for (UINT k = 0; k < N; k++) {
		UINT k1 = (k + 1) % N;
		idx.push_back(apexBase + k); idx.push_back(coneRing + k); idx.push_back(coneRing + k1);
	}
	UINT baseCtr = (UINT)verts.size();
	XMFLOAT3 baseCtrPos, backN; XMStoreFloat3(&baseCtrPos, shaftEnd); XMStoreFloat3(&backN, -dir);
	verts.push_back({ baseCtrPos, backN });
	UINT capRing = (UINT)verts.size();
	for (UINT k = 0; k < N; k++) {
		float a = (float)k / N * XM_2PI;
		XMVECTOR r = xAxis * cosf(a) + yAxis * sinf(a);
		verts.push_back(storeV(shaftEnd + r * headRadius, -dir));
	}
	for (UINT k = 0; k < N; k++) {
		UINT k1 = (k + 1) % N;
		idx.push_back(baseCtr); idx.push_back(capRing + k1); idx.push_back(capRing + k);
	}

	Primitive* arrow = new Primitive(device, deviceContext);
	arrow->SetVertexIndexBuffers(verts.data(), (UINT)verts.size(), idx.data(), (UINT)idx.size(), 2);
	arrow->SetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	arrow->SetPosition({ 0, 0, 0 });
	arrow->SetColor(col);
	arrow->SetIlluminationCapability(true);
	arrow->id = id;
	return arrow;
}

Primitive* PrimitiveConstructor::RevolutionSurface(const std::vector<XMFLOAT3>& profile, UINT segments, const XMFLOAT4& col, UINT id)
{
	if (profile.size() < 2) return nullptr;
	if (segments < 3) segments = 3;

	// Ensure the profile is wound counter-clockwise in the XY plane so that
	// the computed normals point outward and the triangle winding stays
	// consistent. Use the shoelace signed area (treating the profile as a
	// closed loop); negative area means clockwise, so reverse it.
	std::vector<XMFLOAT3> prof = profile;
	{
		float area2 = 0.0f;
		const size_t n = prof.size();
		for (size_t i = 0; i < n; ++i) {
			const XMFLOAT3& a = prof[i];
			const XMFLOAT3& b = prof[(i + 1) % n];
			area2 += a.x * b.y - b.x * a.y;
		}
		if (area2 < 0.0f) std::reverse(prof.begin(), prof.end());
	}

	const UINT P = (UINT)prof.size();

	std::vector<XMFLOAT2> nrm2(P);
	for (UINT i = 0; i < P; ++i) {
		const XMFLOAT3& a = prof[i > 0 ? i - 1 : i];
		const XMFLOAT3& b = prof[i + 1 < P ? i + 1 : i];
		float dx = b.x - a.x;
		float dy = b.y - a.y;
		float nx = dy, ny = -dx;
		float len = sqrtf(nx * nx + ny * ny);
		if (len < 1e-6f) { nx = 1.0f; ny = 0.0f; len = 1.0f; }
		nrm2[i] = { nx / len, ny / len };
	}

	std::vector<Vertex> verts;
	verts.reserve((size_t)P * segments);
	for (UINT i = 0; i < P; ++i) {
		float r = prof[i].x;
		float y = prof[i].y;
		for (UINT j = 0; j < segments; ++j) {
			float a = (float)j / (float)segments * XM_2PI;
			float ca = cosf(a), sa = sinf(a);
			XMFLOAT3 pos = { r * ca, y, r * sa };
			XMFLOAT3 n = { nrm2[i].x * ca, nrm2[i].y, nrm2[i].x * sa };
			XMStoreFloat3(&n, XMVector3Normalize(XMLoadFloat3(&n)));
			verts.push_back({ pos, n });
		}
	}

	std::vector<DWORD> idx;
	idx.reserve((size_t)(P - 1) * segments * 6);
	for (UINT i = 0; i + 1 < P; ++i) {
		for (UINT j = 0; j < segments; ++j) {
			UINT jn = (j + 1) % segments;
			UINT p00 = i * segments + j;
			UINT p01 = i * segments + jn;
			UINT p10 = (i + 1) * segments + j;
			UINT p11 = (i + 1) * segments + jn;
			idx.push_back(p00); idx.push_back(p10); idx.push_back(p01);
			idx.push_back(p10); idx.push_back(p11); idx.push_back(p01);
		}
	}

	Primitive* poly = new Primitive(device, deviceContext);
	poly->SetVertexIndexBuffers(verts.data(), (UINT)verts.size(), idx.data(), (UINT)idx.size(), 2);
	poly->SetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	poly->SetPosition({ 0, 0, 0 });
	poly->SetColor(col);
	poly->SetIlluminationCapability(true);
	poly->id = id;
	return poly;
}




