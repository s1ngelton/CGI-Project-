#include "Renderer.h"
#include "Scene.h"
#include "Primitives.h"
#include <iostream>

Renderer::Renderer(int width, int height)
    : m_width(width), m_height(height) {
    initFramebuffers();
    initShaders();
    initSSAOKernel();
}

Renderer::~Renderer() {
    glDeleteFramebuffers(1, &m_gBuffer);
    glDeleteTextures(1, &m_gPosition);
    glDeleteTextures(1, &m_gNormal);
    glDeleteTextures(1, &m_gAlbedo);
    glDeleteTextures(1, &m_gDepth);
    
    glDeleteFramebuffers(1, &m_shadowFBO);
    glDeleteTextures(1, &m_shadowMap);
    
    glDeleteFramebuffers(1, &m_ssaoFBO);
    glDeleteFramebuffers(1, &m_ssaoBlurFBO);
    glDeleteTextures(1, &m_ssaoColor);
    glDeleteTextures(1, &m_ssaoBlurColor);
    glDeleteTextures(1, &m_ssaoNoiseTexture);
    
    glDeleteFramebuffers(1, &m_hdrFBO);
    glDeleteTextures(1, &m_hdrColor);
    glDeleteFramebuffers(2, m_pingpongFBO);
    glDeleteTextures(2, m_pingpongColor);
    
    if (m_quadVAO) glDeleteVertexArrays(1, &m_quadVAO);
    if (m_quadVBO) glDeleteBuffers(1, &m_quadVBO);
}

void Renderer::initFramebuffers() {
    glGenFramebuffers(1, &m_gBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, m_gBuffer);

    // Position color buffer
    glGenTextures(1, &m_gPosition);
    glBindTexture(GL_TEXTURE_2D, m_gPosition);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, m_width, m_height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_gPosition, 0);

    // Normal color buffer
    glGenTextures(1, &m_gNormal);
    glBindTexture(GL_TEXTURE_2D, m_gNormal);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_width, m_height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_gNormal, 0);

    // Color + Specular/Roughness color buffer
    glGenTextures(1, &m_gAlbedo);
    glBindTexture(GL_TEXTURE_2D, m_gAlbedo);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, m_gAlbedo, 0);

    // Tell OpenGL which color attachments we'll use (of this framebuffer) for rendering 
    unsigned int attachments[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
    glDrawBuffers(3, attachments);

    // Create and attach depth buffer (texture)
    glGenTextures(1, &m_gDepth);
    glBindTexture(GL_TEXTURE_2D, m_gDepth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, m_width, m_height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_gDepth, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "Framebuffer not complete!" << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // --- Shadow Map FBO ---
    glGenFramebuffers(1, &m_shadowFBO);
    glGenTextures(1, &m_shadowMap);
    glBindTexture(GL_TEXTURE_2D, m_shadowMap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SHADOW_RES, SHADOW_RES, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = { 1.0, 1.0, 1.0, 1.0 };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
    
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_shadowMap, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // --- SSAO FBO ---
    glGenFramebuffers(1, &m_ssaoFBO);
    glGenFramebuffers(1, &m_ssaoBlurFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoFBO);
    glGenTextures(1, &m_ssaoColor);
    glBindTexture(GL_TEXTURE_2D, m_ssaoColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, m_width, m_height, 0, GL_RED, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ssaoColor, 0);
    
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoBlurFBO);
    glGenTextures(1, &m_ssaoBlurColor);
    glBindTexture(GL_TEXTURE_2D, m_ssaoBlurColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, m_width, m_height, 0, GL_RED, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ssaoBlurColor, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // --- HDR & Post Process FBOs ---
    glGenFramebuffers(1, &m_hdrFBO);
    glGenTextures(1, &m_hdrColor);
    glBindTexture(GL_TEXTURE_2D, m_hdrColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_width, m_height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_hdrFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_hdrColor, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glGenFramebuffers(2, m_pingpongFBO);
    glGenTextures(2, m_pingpongColor);
    for (unsigned int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_pingpongFBO[i]);
        glBindTexture(GL_TEXTURE_2D, m_pingpongColor[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_width, m_height, 0, GL_RGBA, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_pingpongColor[i], 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

#include <random>

void Renderer::initSSAOKernel() {
    std::uniform_real_distribution<float> randomFloats(0.0, 1.0);
    std::default_random_engine generator;
    for (unsigned int i = 0; i < 64; ++i) {
        glm::vec3 sample(randomFloats(generator) * 2.0 - 1.0, 
                         randomFloats(generator) * 2.0 - 1.0, 
                         randomFloats(generator));
        sample = glm::normalize(sample);
        sample *= randomFloats(generator);
        float scale = float(i) / 64.0f;
        // Scale samples s.t. they're more aligned to center of kernel
        scale = glm::mix(0.1f, 1.0f, scale * scale);
        sample *= scale;
        m_ssaoKernel.push_back(sample);  
    }
    
    std::vector<glm::vec3> ssaoNoise;
    for (unsigned int i = 0; i < 16; i++) {
        glm::vec3 noise(randomFloats(generator) * 2.0 - 1.0, 
                        randomFloats(generator) * 2.0 - 1.0, 
                        0.0f);
        ssaoNoise.push_back(noise);
    }  
    
    glGenTextures(1, &m_ssaoNoiseTexture);
    glBindTexture(GL_TEXTURE_2D, m_ssaoNoiseTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 4, 4, 0, GL_RGB, GL_FLOAT, &ssaoNoise[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);  
}

void Renderer::initShaders() {
    m_gBufferShader = Shader("shaders/gbuffer.vert", "shaders/gbuffer.frag");
    m_lightingShader = Shader("shaders/lighting.vert", "shaders/lighting.frag");
    m_shadowShader = Shader("shaders/shadow.vert", "shaders/shadow.frag");
    m_ssaoShader = Shader("shaders/lighting.vert", "shaders/ssao.frag");
    m_ssaoBlurShader = Shader("shaders/lighting.vert", "shaders/ssao_blur.frag");
    m_dofShader = Shader("shaders/lighting.vert", "shaders/dof.frag");
    m_motionBlurShader = Shader("shaders/lighting.vert", "shaders/motion_blur.frag");
    m_tonemapShader = Shader("shaders/lighting.vert", "shaders/tonemap.frag");
    
    m_lightingShader.use();
    m_lightingShader.setInt("gPosition", 0);
    m_lightingShader.setInt("gNormal", 1);
    m_lightingShader.setInt("gAlbedoSpec", 2);
    m_lightingShader.setInt("shadowMap", 3);
    m_lightingShader.setInt("ssao", 4);
    m_lightingShader.setInt("prevFrameColor", 5);
    
    m_ssaoShader.use();
    m_ssaoShader.setInt("gPosition", 0);
    m_ssaoShader.setInt("gNormal", 1);
    m_ssaoShader.setInt("texNoise", 2);
    
    m_ssaoBlurShader.use();
    m_ssaoBlurShader.setInt("ssaoInput", 0);

    m_dofShader.use();
    m_dofShader.setInt("screenTexture", 0);
    m_dofShader.setInt("gDepth", 1);

    m_motionBlurShader.use();
    m_motionBlurShader.setInt("screenTexture", 0);
    m_motionBlurShader.setInt("gDepth", 1);

    m_tonemapShader.use();
    m_tonemapShader.setInt("hdrBuffer", 0);
}

void Renderer::renderQuad() {
    if (m_quadVAO == 0) {
        float quadVertices[] = {
            // positions        // texture Coords
            -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
             1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
             1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
        };
        glGenVertexArrays(1, &m_quadVAO);
        glGenBuffers(1, &m_quadVBO);
        glBindVertexArray(m_quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    }
    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

void Renderer::passShadow(Scene& scene, const Camera& cam) {
    if (!settings.shadows) return;
    
    // Determine active light source position & direction
    glm::vec3 lightPos = glm::vec3(2.0f, 2.8f, 6.0f); // Default ceiling lamp
    glm::vec3 lightDir = glm::vec3(0.0f, -1.0f, 0.0f); // Pointing straight down
    float fov = 120.0f;
    float farPlane = 15.0f;

    if (settings.lightningActive) {
        lightPos = glm::vec3(-15.0f, 4.0f, 5.0f); // Window lightning flash
        lightDir = glm::normalize(glm::vec3(1.0f, -0.2f, 0.0f)); // Pointing into the room
        fov = 90.0f;
        farPlane = 40.0f;
    }
    
    glm::mat4 lightProjection = glm::perspective(glm::radians(fov), 1.0f, 0.1f, farPlane);
    glm::vec3 up = (std::abs(lightDir.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, -1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::mat4 lightView = glm::lookAt(lightPos, lightPos + lightDir, up);
    glm::mat4 lightSpaceMatrix = lightProjection * lightView;

    m_lightingShader.use();
    m_lightingShader.setMat4("lightSpaceMatrix", lightSpaceMatrix);

    m_shadowShader.use();
    m_shadowShader.setMat4("lightSpaceMatrix", lightSpaceMatrix);

    glViewport(0, 0, SHADOW_RES, SHADOW_RES);
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFBO);
    glClear(GL_DEPTH_BUFFER_BIT);
    
    for (const auto& obj : scene.objects) {
        if (obj.drawAsQuad) continue; // Don't cast shadows from the TV screen quad
        m_shadowShader.setMat4("model", obj.transform);
        glBindVertexArray(obj.vao);
        glDrawArrays(GL_TRIANGLES, 0, obj.vertexCount);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_width, m_height); // Reset viewport
}

void Renderer::passGBuffer(Scene& scene, const Camera& cam) {
    glBindFramebuffer(GL_FRAMEBUFFER, m_gBuffer);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    m_gBufferShader.use();
    
    glm::mat4 projection = glm::perspective(glm::radians(cam.Zoom), (float)m_width / (float)m_height, 0.1f, 100.0f);
    glm::mat4 view = cam.GetViewMatrix();
    m_gBufferShader.setMat4("projection", projection);
    m_gBufferShader.setMat4("view", view);

    for (const auto& obj : scene.objects) {
        m_gBufferShader.setMat4("model", obj.transform);
        m_gBufferShader.setVec3("colorOverride", obj.color);
        m_gBufferShader.setFloat("roughness", obj.roughness);
        m_gBufferShader.setFloat("metallic", obj.metallic);
        m_gBufferShader.setBool("isTVScreen", obj.name == "TV_Screen");
        m_gBufferShader.setFloat("time", m_time);
        
        glBindVertexArray(obj.vao);
        if (obj.drawAsQuad) {
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        } else {
            glDrawArrays(GL_TRIANGLES, 0, obj.vertexCount);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::passSSAO(const Camera& cam) {
    if (!settings.ao) return;
    
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    m_ssaoShader.use();
    
    glm::mat4 projection = glm::perspective(glm::radians(cam.Zoom), (float)m_width / (float)m_height, 0.1f, 100.0f);
    m_ssaoShader.setMat4("projection", projection);
    
    for (unsigned int i = 0; i < 64; ++i)
        m_ssaoShader.setVec3("samples[" + std::to_string(i) + "]", m_ssaoKernel[i]);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_gPosition);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_gNormal);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_ssaoNoiseTexture);
    
    renderQuad();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // SSAO Blur
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoBlurFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    m_ssaoBlurShader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_ssaoColor);
    renderQuad();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::passLighting(Scene& scene, const Camera& cam) {
    glBindFramebuffer(GL_FRAMEBUFFER, m_hdrFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    
    m_lightingShader.use();
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_gPosition);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_gNormal);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_gAlbedo);
    
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_shadowMap);
    glActiveTexture(GL_TEXTURE4);
    if (settings.ao) {
        glBindTexture(GL_TEXTURE_2D, m_ssaoBlurColor);
    } else {
        // Bind an empty/white texture if SSAO is disabled, or handle in shader
        glBindTexture(GL_TEXTURE_2D, 0); 
    }
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, m_pingpongColor[1]); // Bind previous frame's color
    
    // Light properties (Ceiling lamp in living room center)
    glm::vec3 lightPos = glm::vec3(2.0f, 2.8f, 6.0f);
    glm::vec3 lightColor = glm::vec3(0.0f, 0.0f, 0.0f);

    if (settings.roomLightOn) {
        lightColor = glm::vec3(1.0f, 0.9f, 0.8f) * 4.0f; // High-quality PBR room illumination
    }

    if (settings.lightningActive) {
        // High intensity blue-white lightning flash coming from the side window
        lightPos = glm::vec3(-15.0f, 4.0f, 5.0f);
        lightColor = glm::vec3(4.0f, 5.0f, 7.0f) * 120.0f; // Multiplier increased to 120.0 for massive flash
    }

    m_lightingShader.setVec3("lightPos", lightPos);
    m_lightingShader.setVec3("lightColor", lightColor);
    m_lightingShader.setVec3("viewPos", cam.Position);
    m_lightingShader.setBool("shadowsEnabled", settings.shadows);
    m_lightingShader.setBool("pcssEnabled", settings.softShadow);
    m_lightingShader.setBool("ssaoEnabled", settings.ao);
    m_lightingShader.setBool("lightningActive", settings.lightningActive);

    // Pass TV Screen light emission color
    glm::vec3 tvLightColor = glm::vec3(0.0f);
    for (const auto& obj : scene.objects) {
        if (obj.name == "TV_Screen") {
            tvLightColor = obj.color;
            break;
        }
    }
    m_lightingShader.setVec3("tvLightColor", tvLightColor);

    // Pass view/projection matrices for SSR raymarching
    glm::mat4 projection = glm::perspective(glm::radians(cam.Zoom), (float)m_width / (float)m_height, 0.1f, 100.0f);
    glm::mat4 view = cam.GetViewMatrix();
    m_lightingShader.setMat4("projection", projection);
    m_lightingShader.setMat4("view", view);
    
    renderQuad();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::passDOF() {
    if (!settings.dof) {
        // Copy HDR FBO to pingpong 0
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_hdrFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_pingpongFBO[0]);
        glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, m_pingpongFBO[0]);
    glClear(GL_COLOR_BUFFER_BIT);
    m_dofShader.use();
    m_dofShader.setFloat("focusDistance", 10.0f);
    m_dofShader.setFloat("focusRange", 15.0f);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_hdrColor);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_gPosition); // gDepth stored in z component of position or actual depth. Actually depth map is better.
    // Wait, m_gPosition.z is view-space Z! We can use that instead of gDepth for distance.
    
    renderQuad();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::passMotionBlur(const Camera& cam) {
    glm::mat4 projection = glm::perspective(glm::radians(cam.Zoom), (float)m_width / (float)m_height, 0.1f, 100.0f);
    glm::mat4 view = cam.GetViewMatrix();
    glm::mat4 currentViewProj = projection * view;
    glm::mat4 currentViewProjInverse = glm::inverse(currentViewProj);

    if (!settings.motionBlur) {
        // Copy pingpong 0 to pingpong 1
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_pingpongFBO[0]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_pingpongFBO[1]);
        glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        m_prevViewProj = currentViewProj;
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_pingpongFBO[1]);
    glClear(GL_COLOR_BUFFER_BIT);
    m_motionBlurShader.use();
    m_motionBlurShader.setMat4("currentViewProjInverse", currentViewProjInverse);
    m_motionBlurShader.setMat4("prevViewProj", m_prevViewProj);
    m_motionBlurShader.setInt("blurSamples", 10);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_pingpongColor[0]);
    glActiveTexture(GL_TEXTURE1);
    // Actually we need depth buffer for reconstructing position
    glBindTexture(GL_TEXTURE_2D, m_gDepth); // This might not be readable if not configured as texture.
    // Assuming m_gPosition has world pos. The shader used depth. I'll modify the shader to use gPosition!
    
    renderQuad();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_prevViewProj = currentViewProj;
}

void Renderer::passTonemap() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_tonemapShader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_pingpongColor[1]);
    renderQuad();
}

void Renderer::render(Scene& scene, const Camera& camera, float deltaTime) {
    m_time += deltaTime;
    // 0. Shadow Pass
    passShadow(scene, camera);

    // 1. Geometry Pass: render scene's geometry/color data into G-buffer
    passGBuffer(scene, camera);
    
    // 2. SSAO Pass
    passSSAO(camera);
    
    // 3. Lighting Pass: calculate lighting by iterating over a screen filled quad
    passLighting(scene, camera);
    
    // 4. Post-Process Passes
    passDOF();
    passMotionBlur(camera);
    passTonemap();
}