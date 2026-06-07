#define _USE_MATH_DEFINES
#include <random>
#include <algorithm>
#include <cmath>

#include <cstdlib>
#include "our_gl.h"
#include "model.h"

extern mat<4, 4> Viewport, ModelView, Perspective;
extern std::vector<double> zbuffer;

struct BlankShader : IShader {
	const Model& model;

	BlankShader(const Model& m) : model(m) {}

	virtual vec4 vertex(const int face, const int vert) {
		vec4 gl_Position = ModelView * model.vert(face, vert);
		return Perspective * gl_Position;
	}

	virtual std::pair<bool, TGAColor> fragment(const vec3 bar) const {
		return { false, {255, 255, 255, 255} };
	}
};

struct PhongShader : IShader {
	const Model& model;
	vec4 l;
	vec2 varying_uv[3];
	vec4 varying_nrm[3];
	vec4 tri[3];

	PhongShader(const vec3 light, const Model& m) : model(m) {
		l = normalized((ModelView * vec4{ light.x, light.y, light.z, 0.0 }));
	}

	virtual vec4 vertex(const int face, const int vert) {
		varying_uv[vert] = model.uv(face, vert);
		varying_nrm[vert] = ModelView.invert_transpose() * model.normal(face, vert);
		vec4 gl_Position = ModelView * model.vert(face, vert);
		tri[vert] = gl_Position;

		return Perspective * gl_Position;
	}

	virtual std::pair<bool, TGAColor> fragment(const vec3 bar) const {
		mat<2, 4> E = { tri[1] - tri[0], tri[2] - tri[0] };
		mat<2, 2> U = { varying_uv[1] - varying_uv[0], varying_uv[2] - varying_uv[0] };
		mat<2, 4> T = U.invert() * E;
		mat<4, 4> D = {
			normalized(T[0]),
			normalized(T[1]),
			normalized(varying_nrm[0] * bar[0] + varying_nrm[1] * bar[1] + varying_nrm[2] * bar[2]),
			{0, 0, 0, 1}
		};
		vec2 uv = varying_uv[0] * bar[0] + varying_uv[1] * bar[1] + varying_uv[2] * bar[2];
		vec4 n = normalized(D.transpose() * model.normal(uv));
		vec4 r = normalized(n * (n * l) * 2 - l);

		double ambient = 0.3;
		double diff = std::max(0.0, n * l);
		double spec = (1.0 + 3.0 * sample2D(model.specular(), uv)[0]/255.0) * std::pow(std::max(r.z, 0.0), 35);
		TGAColor gl_FragColor = sample2D(model.diffuse(), uv);
		for (int channel : {0, 1, 2}) {
			gl_FragColor[channel] *= std::min(1.0, ambient + 0.4 * diff + 0.9 * spec);
		}
		return { false, gl_FragColor };
	}
};

void drop_zbuffer(std::string filename, std::vector<double>& zbuffer, int width, int height) {
	TGAImage zimg(width, height, TGAImage::GRAYSCALE, { 0,0,0,0 });
	double minz = +1000;
	double maxz = -1000;
	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
			double z = zbuffer[x + y * width];
			if (z < -100) continue;
			minz = std::min(z, minz);
			maxz = std::max(z, maxz);
		}
	}
	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
			double z = zbuffer[x + y * width];
			if (z < -100) continue;
			z = (z - minz) / (maxz - minz) * 255;
			zimg.set(x, y, { static_cast<unsigned char>(z), 255, 255, 255 });
		}
	}
	zimg.write_tga_file(filename);
}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "Usage: " << argv[0] << " obj/model.obj" << std::endl;
		return 1;
	}

	constexpr int width = 800;
	constexpr int height = 800;
	constexpr int shadoww = 8000;
	constexpr int shadowh = 8000;
	constexpr vec3 light{ 1, 1, 1 };
	constexpr vec3 eye{ -1, 0, 2 };
	constexpr vec3 center{ 0, 0, 0 };
	constexpr vec3 up{ 0, 1, 0 };

	// standard rendering pass
	lookat(eye, center, up);
	init_perspective(norm(eye - center));
	init_viewport(width / 16, height / 16, width * 7 / 8, height * 7 / 8);
	init_zbuffer(width, height);
	TGAImage framebuffer(width, height, TGAImage::RGB, { 177, 195, 209, 255 });

	for (int m = 1; m < argc; m++) {
		Model model(argv[m]);
		PhongShader shader(light, model);
		for (int f = 0; f < model.nfaces(); f++) {
			Triangle clip = {
				shader.vertex(f, 0),
				shader.vertex(f, 1),
				shader.vertex(f, 2)
			};
			rasterize(clip, shader, framebuffer);
		}
	}

	constexpr double ao_radius = 0.1;
	constexpr int nsamples = 128;
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_real_distribution<double> dist(-ao_radius, ao_radius);
	auto smoothstep = [](double edge0, double edge1, double x) {
		double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
		return t * t * (3 - 2 * t);
	};
	
#pragma omp parallel for
	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
			double z = zbuffer[x + y * width];
			if (z < -100) continue;
			vec4 fragment = Viewport.invert() * vec4 { (double)x, (double)y, z, 1.0 };
			double vote = 0;
			double voters = 0;
			for (int i = 0; i < nsamples; i++) {
				vec4 p = Viewport * (fragment + vec4{ dist(gen), dist(gen), dist(gen), 0.0 });
				if (p.x < 0 || p.x >= width || p.y < 0 || p.y >= height) continue;
				double d = zbuffer[int(p.x) + int(p.y) * width];
				if (z + 5 * ao_radius < d) continue;
				voters++;
				vote += d > p.z;
			}
			double ssao = smoothstep(0, 1, 1 - vote / voters * 0.4);
			TGAColor c = framebuffer.get(x, y);
			framebuffer.set(x, y, { static_cast<unsigned char>(c[0] * ssao), static_cast<unsigned char>(c[1] * ssao), static_cast<unsigned char>(c[2] * ssao), c[3] });
		}
	}

	framebuffer.write_tga_file("framebuffer.tga");
	return 0;
}