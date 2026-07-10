#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

class ComputeShader {
public:
    unsigned int ID = 0;

    ComputeShader() = default;

    ComputeShader(const char* compPath,
           const char* geomPath = nullptr) {
        std::string CCode = readFile(compPath);

        unsigned int comp = compile(GL_COMPUTE_SHADER,   CCode.c_str());

        ID = glCreateProgram();
        glAttachShader(ID, comp);
        glLinkProgram(ID);
        checkErrors(ID, "PROGRAM");
        glDeleteShader(comp);
    }

    void use() const { glUseProgram(ID); }

    // Uniform setters
    void setBool (const std::string& n, bool v)          const { glUniform1i (loc(n), (int)v); }
    void setInt  (const std::string& n, int v)           const { glUniform1i (loc(n), v); }
    void setUInt  (const std::string& n, unsigned int v)  const { glUniform1ui (loc(n), v); }
    void setFloat(const std::string& n, float v)         const { glUniform1f (loc(n), v); }
    void setVec2 (const std::string& n, glm::vec2 v)     const { glUniform2fv(loc(n), 1, glm::value_ptr(v)); }
    void setVec3 (const std::string& n, glm::vec3 v)     const { glUniform3fv(loc(n), 1, glm::value_ptr(v)); }
    void setVec4 (const std::string& n, glm::vec4 v)     const { glUniform4fv(loc(n), 1, glm::value_ptr(v)); }
    void setMat4 (const std::string& n, glm::mat4 v)     const { glUniformMatrix4fv(loc(n), 1, GL_FALSE, glm::value_ptr(v)); }

private:
    GLint loc(const std::string& name) const {
        return glGetUniformLocation(ID, name.c_str());
    }

    static std::string readFile(const char* path) {
        std::ifstream f(path);
        if (!f.is_open()) {
            std::cerr << "Shader file not found: " << path << "\n";
            return "";
        }
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    static unsigned int compile(GLenum type, const char* src) {
        unsigned int id = glCreateShader(type);
        glShaderSource(id, 1, &src, nullptr);
        glCompileShader(id);
        checkErrors(id, "COMPUTE");
        return id;
    }

    static void checkErrors(unsigned int id, const std::string& type) {
        int  success;
        char log[1024];
        if (type != "PROGRAM") {
            glGetShaderiv(id, GL_COMPILE_STATUS, &success);
            if (!success) {
                glGetShaderInfoLog(id, 1024, nullptr, log);
                std::cerr << "[SHADER ERROR:" << type << "]\n" << log << "\n";
                
            }
        } else {
            glGetProgramiv(id, GL_LINK_STATUS, &success);
            if (!success) {
                glGetProgramInfoLog(id, 1024, nullptr, log);
                std::cerr << "[PROGRAM LINK ERROR]\n" << log << "\n";
            }
        }
    }
};
