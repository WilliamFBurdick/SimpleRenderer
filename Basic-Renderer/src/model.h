#pragma once
#include <vector>
#include "geometry.h"
#include "tgaimage.h"

class Model {
	std::vector<vec4> verts = {};
	std::vector<vec4> norms = {};
	std::vector<vec2> tex = {};
	std::vector<int> facet_vrt = {};
	std::vector<int> facet_nrm = {};
	std::vector<int> facet_tex = {};
	TGAImage normalmap = {};
	TGAImage diffusemap = {};
public:
	Model(const std::string filename);
	int nverts() const;
	int nfaces() const;
	vec4 vert(const int i) const;
	vec4 vert(const int iface, const int nthvert) const;
	vec4 normal(const int iface, const int nthvert) const;
	vec4 normal(const vec2& uv) const;
	TGAColor diffuse(const vec2& uv) const;
	vec2 uv(const int iface, const int nthvert) const;
};