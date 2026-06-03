#include <cstdlib>
#include "our_gl.h"
#include "model.h"

extern mat<4, 4> ModelView, Perspective;
extern std::vector<double> zbuffer;

struct RandomShader : IShader {
	const Model& model;
	TGAColor color = {}; 
	vec3 tri[3];	// triangle in eye coordinates

	RandomShader(const Model& m) : model(m) {

	}

	virtual vec4 vertex(const int face, const int vert) {
		vec4 v = model.vert(face, vert);
		vec4 gl_Position = ModelView * vec4{ v.x, v.y, v.z, 1.0 };
		tri[vert] = gl_Position.xyz();
		return Perspective * gl_Position;
	}

	virtual std::pair<bool, TGAColor> fragment(const vec3 bar) const {
		return { false, color };
	}
};

struct PhongShader : IShader {
	const Model& model;
	vec4 l;
	vec2 varying_uv[3];

	PhongShader(const vec3 light, const Model& m) : model(m) {
		l = normalized((ModelView * vec4{ light.x, light.y, light.z, 0.0 }));
	}

	virtual vec4 vertex(const int face, const int vert) {
		varying_uv[vert] = model.uv(face, vert);
		vec4 gl_Position = ModelView * model.vert(face, vert);

		return Perspective * gl_Position;
	}

	virtual std::pair<bool, TGAColor> fragment(const vec3 bar) const {
		TGAColor gl_FragColor = { 255, 255, 255, 255 };
		vec2 uv = varying_uv[0] * bar[0] + varying_uv[1] * bar[1] + varying_uv[2] * bar[2];
		vec4 n = normalized(ModelView.invert_transpose() * model.normal(uv));
		vec4 r = normalized(n * (n * l) * 2 - l);

		gl_FragColor = model.diffuse(uv);
		double ambient = 0.3;
		double diff = std::max(0.0, n * l);
		double spec = std::pow(std::max(r.z, 0.0), 35);
		for (int channel : {0, 1, 2}) {
			gl_FragColor[channel] *= std::min(1.0, ambient + 0.4 * diff + 0.9 * spec);
		}
		return { false, gl_FragColor };
	}
};

int main(int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "Usage: " << argv[0] << " obj/model.obj" << std::endl;
		return 1;
	}

	constexpr int width = 800;
	constexpr int height = 800;
	constexpr vec3 light{ 1, 1, 1 };
	constexpr vec3 eye{ -1, 0, 2 };
	constexpr vec3 center{ 0, 0, 0 };
	constexpr vec3 up{ 0, 1, 0 };

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
	return 0;
}