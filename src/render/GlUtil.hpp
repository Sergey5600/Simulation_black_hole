#pragma once

#include <glad/gl.h>

#include <glm/glm.hpp>
#include <initializer_list>
#include <string>
#include <unordered_map>

namespace bh::gl {

// A linked GLSL program. Each shader stage is assembled from several snippets
// (e.g. common.glsl + points.vert.glsl) behind a shared #version line.
class Program {
 public:
  Program() = default;
  ~Program();
  Program(const Program&) = delete;
  Program& operator=(const Program&) = delete;

  bool build(const char* debugName, std::initializer_list<const char*> vertexParts,
             std::initializer_list<const char*> fragmentParts);
  bool buildCompute(const char* debugName, std::initializer_list<const char*> parts);
  void use() const { glUseProgram(id_); }
  GLuint id() const { return id_; }

  void set(const char* name, int v);
  void set(const char* name, unsigned v);
  void set(const char* name, bool v) { set(name, v ? 1 : 0); }
  void set(const char* name, float v);
  void set(const char* name, const glm::vec2& v);
  void set(const char* name, const glm::vec3& v);
  void set(const char* name, const glm::vec4& v);
  void set(const char* name, const glm::mat4& m);

 private:
  GLint location(const char* name);

  GLuint id_ = 0;
  std::unordered_map<std::string, GLint> locations_;
};

// Color texture (+ optional depth renderbuffer) bound to a framebuffer.
struct RenderTarget {
  GLuint fbo = 0;
  GLuint color = 0;
  GLuint depth = 0;
  int width = 0;
  int height = 0;

  void create(int w, int h, GLenum internalFormat, bool withDepth);
  void destroy();
};

}  // namespace bh::gl
