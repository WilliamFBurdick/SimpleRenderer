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

	framebuffer.write_tga_file("framebuffer.tga");
	drop_zbuffer("zbuffer1.tga", zbuffer, width, height);

	std::vector<bool> mask(width * height, false);
	std::vector<double> zbuffer_copy = zbuffer;
	mat<4, 4> M = (Viewport * Perspective * ModelView).invert();

	{
		// Shadow rendering pass
		lookat(light, center, up);
		init_perspective(norm(eye - center));
		init_viewport(shadoww / 16, shadowh / 16, shadoww * 7 / 8, shadowh * 7 / 8);
		init_zbuffer(shadoww, shadowh);
		TGAImage trash(shadoww, shadowh, TGAImage::RGB, { 177, 195, 209, 255 });

		for (int m = 1; m < argc; m++) {
			Model model(argv[m]);
			BlankShader shader{ model };
			for (int f = 0; f < model.nfaces(); f++) {
				Triangle clip = {
					shader.vertex(f, 0),
					shader.vertex(f, 1),
					shader.vertex(f, 2)
				};
				rasterize(clip, shader, trash);
			}
		}
		trash.write_tga_file("shadowmap.tga");
	}
	drop_zbuffer("zbuffer2.tga", zbuffer, shadoww, shadowh);

	mat<4, 4> N = Viewport * Perspective * ModelView;

	// Post-Processing pass
	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
			vec4 fragment = M * vec4{ (double)x, (double)y, zbuffer_copy[x + y * width], 1.0 };
			vec4 q = N * fragment;
			vec3 p = q.xyz() / q.w;
			bool lit = (
				fragment.z < -100
				|| (p.x < 0 || p.x >= shadoww || p.y < 0 || p.y >= shadowh)
				|| (p.z > zbuffer[int(p.x) + int(p.y) * shadoww] - 0.03)
				);
			mask[x + y * width] = lit;
		}
	}

	TGAImage maskimg(width, height, TGAImage::GRAYSCALE);
	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
			if (mask[x + y * width]) continue;
			maskimg.set(x, y, { 255, 255, 255, 255 });
		}
	}
	maskimg.write_tga_file("mask.tga");

	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
			if (mask[x + y * width]) continue;
			TGAColor c = framebuffer.get(x, y);
			vec3 a = { c[0], c[1], c[2] };
			if (norm(a) < 80) continue;
			a = normalized(a) * 80;
			framebuffer.set(x, y, { static_cast<unsigned char>(a[0]), static_cast<unsigned char>(a[1]), static_cast<unsigned char>(a[2]), 255 });
		}
	}
	framebuffer.write_tga_file("shadow.tga");
	return 0;
}