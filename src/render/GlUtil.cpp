#include "render/GlUtil.hpp"

#include <cstdio>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

namespace bh::gl {

namespace {

constexpr const char* kVersion = "#version 430 core\n";

GLuint compileStage(GLenum stage, std::initializer_list<const char*> parts, const char* debugName) {
  std::vector<const char*> sources{kVersion};
  sources.insert(sources.end(), parts.begin(), parts.end());

  const GLuint shader = glCreateShader(stage);
  glShaderSource(shader, static_cast<GLsizei>(sources.size()), sources.data(), nullptr);
  glCompileShader(shader);

  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    std::fprintf(stderr, "[shader] %s (%s) failed to compile:\n%s\n", debugName,
                 stage == GL_VERTEX_SHADER ? "vertex" : stage == GL_COMPUTE_SHADER ? "compute" : "fragment", log);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

}  // namespace

Program::~Program() {
  if (id_) glDeleteProgram(id_);
}

bool Program::build(const char* debugName, std::initializer_list<const char*> vertexParts,
                    std::initializer_list<const char*> fragmentParts) {
  const GLuint vs = compileStage(GL_VERTEX_SHADER, vertexParts, debugName);
  const GLuint fs = compileStage(GL_FRAGMENT_SHADER, fragmentParts, debugName);
  if (!vs || !fs) return false;

  id_ = glCreateProgram();
  glAttachShader(id_, vs);
  glAttachShader(id_, fs);
  glLinkProgram(id_);
  glDeleteShader(vs);
  glDeleteShader(fs);

  GLint ok = GL_FALSE;
  glGetProgramiv(id_, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetProgramInfoLog(id_, sizeof(log), nullptr, log);
    std::fprintf(stderr, "[shader] %s failed to link:\n%s\n", debugName, log);
    glDeleteProgram(id_);
    id_ = 0;
    return false;
  }
  return true;
}

bool Program::buildCompute(const char* debugName, std::initializer_list<const char*> parts) {
  const GLuint cs = compileStage(GL_COMPUTE_SHADER, parts, debugName);
  if (!cs) return false;
  id_ = glCreateProgram();
  glAttachShader(id_, cs);
  glLinkProgram(id_);
  glDeleteShader(cs);
  GLint ok = GL_FALSE;
  glGetProgramiv(id_, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetProgramInfoLog(id_, sizeof(log), nullptr, log);
    std::fprintf(stderr, "[shader] %s failed to link:\n%s\n", debugName, log);
    glDeleteProgram(id_);
    id_ = 0;
    return false;
  }
  return true;
}

GLint Program::location(const char* name) {
  const auto it = locations_.find(name);
  if (it != locations_.end()) return it->second;
  const GLint loc = glGetUniformLocation(id_, name);
  locations_.emplace(name, loc);
  return loc;
}

void Program::set(const char* name, int v) { glUniform1i(location(name), v); }
void Program::set(const char* name, unsigned v) { glUniform1ui(location(name), v); }
void Program::set(const char* name, float v) { glUniform1f(location(name), v); }
void Program::set(const char* name, const glm::vec2& v) { glUniform2fv(location(name), 1, glm::value_ptr(v)); }
void Program::set(const char* name, const glm::vec3& v) { glUniform3fv(location(name), 1, glm::value_ptr(v)); }
void Program::set(const char* name, const glm::vec4& v) { glUniform4fv(location(name), 1, glm::value_ptr(v)); }
void Program::set(const char* name, const glm::mat3& m) {
  glUniformMatrix3fv(location(name), 1, GL_FALSE, glm::value_ptr(m));
}
void Program::set(const char* name, const glm::vec3* values, int count) {
  glUniform3fv(location(name), count, glm::value_ptr(values[0]));
}
void Program::set(const char* name, const glm::mat4& m) {
  glUniformMatrix4fv(location(name), 1, GL_FALSE, glm::value_ptr(m));
}

void RenderTarget::create(int w, int h, GLenum internalFormat, bool withDepth) {
  destroy();
  width = w;
  height = h;

  glGenTextures(1, &color);
  glBindTexture(GL_TEXTURE_2D, color);
  glTexStorage2D(GL_TEXTURE_2D, 1, internalFormat, w, h);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);

  if (withDepth) {
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
  }

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::fprintf(stderr, "[gl] framebuffer %dx%d incomplete\n", w, h);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTarget::destroy() {
  if (fbo) glDeleteFramebuffers(1, &fbo);
  if (color) glDeleteTextures(1, &color);
  if (depth) glDeleteRenderbuffers(1, &depth);
  fbo = color = depth = 0;
  width = height = 0;
}

}  // namespace bh::gl
