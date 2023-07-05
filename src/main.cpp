#include <GL/glew.h>
#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <skeletal_mesh.h>

#include <string>
#include <iostream>
#include <random>
#include <numbers>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

constexpr int SCREEN_WIDTH = 800;
constexpr int SCREEN_HEIGHT = 600;

const char* geometryVS =
    "#version 410\n"
    "layout (location = 0) in vec3 aPos;\n"
    "layout (location = 1) in vec2 aTexCoords;\n"
    "layout (location = 2) in vec3 aNormal;\n"
    "layout (location = 3) in ivec4 aBoneIndex;\n"
    "layout (location = 4) in vec4 aBoneWeight;\n"
    "out vec3 FragPos;\n"
    "out vec2 TexCoords;\n"
    "out vec3 Normal;\n"
    "uniform bool invertedNormals;\n"
    "uniform mat4 model;\n"
    "uniform mat4 view;\n"
    "uniform mat4 projection;\n"
    "void main() {\n"
    "    vec4 viewPos = view * model * vec4(aPos, 1.0);\n"
    "    FragPos = viewPos.xyz;\n"
    "    TexCoords = aTexCoords;\n"
    "    Normal = transpose(inverse(mat3(view * model))) * (invertedNormals ? -aNormal : aNormal);\n"
    "    gl_Position = projection * viewPos;\n"
    "}\n";

const char* geometryFS =
    "#version 410\n"
    "in vec3 FragPos;\n"
    "in vec2 TexCoords;\n"
    "in vec3 Normal;\n"
    "out vec3 gPosition;\n"
    "out vec3 gNormal;\n"
    "out vec3 gAlbedo;\n"
    "void main() {\n"
    "    gPosition = FragPos;\n"
    "    gNormal = normalize(Normal);\n"
    // "    gAlbedo.rgb = vec3(0.95);\n"
    "    gAlbedo.rgb = vec3(TexCoords, 1.0);\n"
    "}\n";

const char* ssaoVS =
    "#version 410\n"
    "layout (location = 0) in vec3 aPos;\n"
    "layout (location = 1) in vec2 aTexCoords;\n"
    "out vec2 TexCoords;\n"
    "void main() {\n"
    "    TexCoords = aTexCoords;\n"
    "    gl_Position = vec4(aPos, 1.0);\n"
    "}\n";

const char* ssaoFS =
    "#version 410\n"
    "uniform sampler2D gPosition;\n"
    "uniform sampler2D gNormal;\n"
    "uniform sampler2D texNoise;\n"
    "uniform vec3 kernel[64];\n"
    "uniform mat4 projection;\n"
    "const float radius = 1.0;\n"
    "const float bias = 0.05;\n"
    "const vec2 noiseScale = vec2(800.0/4.0, 600.0/4.0);\n" 
    "in vec2 TexCoords;\n"
    "out float ssaoResult;\n"
    "void main() {\n"
    "    vec3 fragPos = texture(gPosition, TexCoords).xyz;\n"
    "    vec3 normal = normalize(texture(gNormal, TexCoords).rgb);\n"
    "    vec3 randomVec = normalize(texture(texNoise, TexCoords * noiseScale).xyz);\n"
    "    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));\n"
    "    vec3 bitangent = cross(normal, tangent);\n"
    "    mat3 TBN = mat3(tangent, bitangent, normal);\n"
    "    float occlusion = 0.0;\n"
    "    for (int i = 0; i < 64; i++) {\n"
    "        vec3 samplePos = fragPos + TBN * kernel[i] * radius;\n"
    "        vec4 screenPos = projection * vec4(samplePos, 1.0);\n"
    "        screenPos.xyz /= screenPos.w;\n"
    "        screenPos.xyz = screenPos.xyz * 0.5 + 0.5;\n"
    "        float sampleDepth = texture(gPosition, screenPos.xy).z;\n"
    "        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampleDepth));\n"
    "        occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;\n"
    "    }\n"
    "    ssaoResult = 1.0 - occlusion / 64.0;\n"
    "}\n";

const char* ssaoBlurFS =
    "#version 410\n"
    "uniform sampler2D ssaoInput;\n"
    "uniform sampler2D gAlbedo;\n"
    "in vec2 TexCoords;\n"
    "out vec3 FragColor;\n"
    "void main() {\n"
    "    vec2 texelSize = 1.0 / vec2(textureSize(ssaoInput, 0));\n"
    "    float result = 0.0;\n"
    "    for (int x = -2; x < 2; x++) {\n"
    "        for (int y = -2; y < 2; y++) {\n"
    "            vec2 offset = vec2(float(x), float(y)) * texelSize;\n"
    "            result += texture(ssaoInput, TexCoords + offset).r;\n"
    "        }\n"
    "    }\n"
    "    FragColor = texture(gAlbedo, TexCoords).rgb * vec3(result / (4.0 * 4.0));\n"
    "}\n";

void keyCallback(GLFWwindow* window,
                 int key,
                 int scancode,
                 int action,
                 int mode);

void mouseCallback(GLFWwindow* window, double xpos, double ypos);

void doMovement(float timePeriod);

unsigned createProgram(const char* VSSource, const char* FSSource);
void renderQuad();
void renderCube();

glm::vec3 lightPos = glm::vec3(0.0f, 0.0f, 3.0f);
glm::vec3 lightDir = glm::vec3(0.0f, 0.0f, -1.0f);

float lastX = SCREEN_WIDTH * 0.5f;
float lastY = SCREEN_HEIGHT * 0.5f;
float yaw = 90.0f;
float pitch = 0.0f;
float fov = 45.0f;

glm::vec3 cameraPos = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, 1.0f);
glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);

int main(int argc, char** argv) {
  std::string modelName = argc == 1 ? "car" : argv[1];
  // 初始化
  glfwInit();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  // 创建窗口
  GLFWwindow* window =
      glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "SSDO", nullptr, nullptr);
  if (window == nullptr) {
    glfwTerminate();
    std::exit(EXIT_FAILURE);
  }

  glfwMakeContextCurrent(window);

  glfwSetKeyCallback(window, keyCallback);
  glfwSetCursorPosCallback(window, mouseCallback);

  if (glewInit() != GLEW_OK) {
    std::exit(EXIT_FAILURE);
  }

  // 编译链接着色器
  unsigned geometryProgram = createProgram(geometryVS, geometryFS);
  unsigned ssaoProgram = createProgram(ssaoVS, ssaoFS);
  unsigned ssaoBlurProgram = createProgram(ssaoVS, ssaoBlurFS);

  // 导入模型
  SkeletalMesh::Scene& sr =
      SkeletalMesh::Scene::loadScene(modelName, "resources/" + modelName + ".fbx");
  if (&sr == &SkeletalMesh::Scene::error)
    std::cout << "Error occured in loadMesh()" << std::endl;

  sr.setShaderInput(geometryProgram, "aPos", "aTexCoords", "aNormal",
                    "aBoneIndex", "aBoneWeight");

  // 创建 gBuffer 以及贴图
  unsigned gBuffer;
  glGenFramebuffers(1, &gBuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, gBuffer);
  unsigned gPosition, gNormal, gAlbedo;
  // position color buffer
  glGenTextures(1, &gPosition);
  glBindTexture(GL_TEXTURE_2D, gPosition);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, SCREEN_WIDTH, SCREEN_HEIGHT, 0, GL_RGBA,
               GL_FLOAT, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         gPosition, 0);
  // normal color buffer
  glGenTextures(1, &gNormal);
  glBindTexture(GL_TEXTURE_2D, gNormal);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, SCREEN_WIDTH, SCREEN_HEIGHT, 0,
               GL_RGBA,
               GL_FLOAT, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
                         gNormal, 0);
  // color + specular color buffer
  glGenTextures(1, &gAlbedo);
  glBindTexture(GL_TEXTURE_2D, gAlbedo);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SCREEN_WIDTH, SCREEN_HEIGHT, 0,
               GL_RGBA,
               GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D,
                         gAlbedo, 0);
  // tell OpenGL which color attachments we'll use (of this framebuffer) for
  // rendering
  unsigned attachments[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
                                 GL_COLOR_ATTACHMENT2};
  glDrawBuffers(3, attachments);
  // create and attach depth buffer (renderbuffer)
  unsigned rboDepth;
  glGenRenderbuffers(1, &rboDepth);
  glBindRenderbuffer(GL_RENDERBUFFER, rboDepth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, SCREEN_WIDTH,
                        SCREEN_HEIGHT);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            GL_RENDERBUFFER, rboDepth);
  // finally check if framebuffer is complete
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    std::cout << "Framebuffer not complete!" << std::endl;

  unsigned ssaoBuffer;
  glGenFramebuffers(1, &ssaoBuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, ssaoBuffer);
  unsigned int ssaoColorBuffer;
  // SSAO color buffer
  glGenTextures(1, &ssaoColorBuffer);
  glBindTexture(GL_TEXTURE_2D, ssaoColorBuffer);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, SCREEN_WIDTH, SCREEN_HEIGHT, 0, GL_RED,
               GL_FLOAT, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         ssaoColorBuffer, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    std::cout << "SSAO Framebuffer not complete!" << std::endl;

  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  glEnable(GL_DEPTH_TEST);
  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

  glUseProgram(ssaoProgram);
  glUniform1i(glGetUniformLocation(ssaoProgram, "gPosition"), 0);
  glUniform1i(glGetUniformLocation(ssaoProgram, "gNormal"), 1);
  glUniform1i(glGetUniformLocation(ssaoProgram, "texNoise"), 2);

  glUseProgram(ssaoBlurProgram);
  glUniform1i(glGetUniformLocation(ssaoBlurProgram, "ssaoInput"), 0);
  glUniform1i(glGetUniformLocation(ssaoBlurProgram, "gAlbedo"), 1);

  // 随机取样
  std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
  std::default_random_engine generator;
  std::vector<glm::vec3> ssaoKernel(64);
  for (int i = 0; i < 64; i++) {
    float x = randomFloats(generator) * 2.0f - 1.0f;
    float y = randomFloats(generator) * 2.0f - 1.0f;
    float z = randomFloats(generator);

    glm::vec3 sample(x, y, z);
    sample = normalize(sample);
    float scale = float(i) / 64.0f;
    scale = 0.1 + 0.9 * scale * scale;
    sample *= scale;

    ssaoKernel[i] = sample;
  }

  // 随机旋转
  std::vector<glm::vec3> ssaoNoise;
  for (unsigned int i = 0; i < 16; i++) {
    glm::vec3 noise(randomFloats(generator) * 2.0 - 1.0,
                    randomFloats(generator) * 2.0 - 1.0,
                    0.0f);
    ssaoNoise.push_back(noise);
  }
  unsigned noiseTexture;
  glGenTextures(1, &noiseTexture);
  glBindTexture(GL_TEXTURE_2D, noiseTexture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 4, 4, 0, GL_RGB, GL_FLOAT,
               &ssaoNoise[0]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

  cameraPos = glm::vec3(-4.79442f, 1.11827f, 0.0814787f);
  yaw = 50.8499f;
  pitch = -20.0f;
  glm::vec3 front;
  front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
  front.y = sin(glm::radians(pitch));
  front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
  cameraFront = glm::normalize(front);

  float lastTime = glfwGetTime();
  while (!glfwWindowShouldClose(window)) {
    float curTime = glfwGetTime();

    glfwPollEvents();
    doMovement(curTime - lastTime);

    lastTime = curTime;

    float ratio;
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    ratio = width / (float)height;
    glViewport(0, 0, width, height);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Geometry Pass
    glBindFramebuffer(GL_FRAMEBUFFER, gBuffer);
    glUseProgram(geometryProgram);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glm::mat4 model(1.0f), view(1.0f), projection(1.0f);
    model = glm::translate(model, glm::vec3(0.0f, -3.0f, 8.0f));
    model = glm::scale(model, glm::vec3(0.02f));
    model = glm::rotate(model, std::numbers::pi_v<float> * -0.5f,
                        glm::vec3(1.0f, 0.0f, 0.0f));
    view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    projection = glm::perspective(glm::radians(fov), ratio,
                                  0.1f, 100.0f);
    glUniformMatrix4fv(glGetUniformLocation(geometryProgram, "model"), 1,
                       GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(glGetUniformLocation(geometryProgram, "view"), 1,
                       GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(geometryProgram, "projection"), 1,
                       GL_FALSE, glm::value_ptr(projection));
    glUniform1i(glGetUniformLocation(geometryProgram, "invertedNormals"), 0);
    sr.render();
    model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 8.0f));
    model = glm::scale(model, glm::vec3(10.0f));
    glUniformMatrix4fv(glGetUniformLocation(geometryProgram, "model"), 1,
                       GL_FALSE, glm::value_ptr(model));
    glUniform1i(glGetUniformLocation(geometryProgram, "invertedNormals"), 1);
    renderCube();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // SSAO PASS
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoBuffer);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(ssaoProgram);
    for (unsigned int i = 0; i < 64; ++i) {
      glUniform3fv(
          glGetUniformLocation(ssaoProgram,
                               ("kernel[" + std::to_string(i) + "]").c_str()),
          1, glm::value_ptr(ssaoKernel[i]));
    }
    glUniformMatrix4fv(glGetUniformLocation(ssaoProgram, "projection"), 1,
                       GL_FALSE, glm::value_ptr(projection));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gPosition);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gNormal);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, noiseTexture);
    renderQuad();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // SSAO Blur PASS
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(ssaoBlurProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ssaoColorBuffer);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gAlbedo);
    renderQuad();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glfwSwapBuffers(window);
  }

  SkeletalMesh::Scene::unloadScene(modelName);
  glfwDestroyWindow(window);
  glfwTerminate();
  exit(EXIT_SUCCESS);
}

bool keyPressed[1024];
bool lightfollow = false;

void keyCallback(GLFWwindow *window,
                 int key,
                 int scancode,
                 int action,
                 int mode) {
  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
    glfwSetWindowShouldClose(window, true);
  }
  if (key == GLFW_KEY_SPACE && action == GLFW_PRESS) {
    lightfollow ^= 1;
  }

  if (0 <= key && key < 1024) {
    if (action == GLFW_PRESS) {
      keyPressed[key] = true;
    }
    if (action == GLFW_RELEASE) {
      keyPressed[key] = false;
    }
  }
}

float cameraSpeed = 3.0f;
void doMovement(float timePeriod) {
  float distance = timePeriod * cameraSpeed;
  if (keyPressed[GLFW_KEY_W]) {
    cameraPos += distance * cameraFront;
  }
  if (keyPressed[GLFW_KEY_S]) {
    cameraPos -= distance * cameraFront;
  }
  if (keyPressed[GLFW_KEY_A]) {
    cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * distance;
  }
  if (keyPressed[GLFW_KEY_D]) {
    cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * distance;
  }
  if (keyPressed[GLFW_KEY_SPACE]) {
    cameraPos += cameraUp * distance;
  }
  if (keyPressed[GLFW_KEY_LEFT_SHIFT]) {
    cameraPos -= cameraUp * distance;
  }
  if (lightfollow) {
    lightPos = cameraPos;
    lightDir = cameraFront;
  }
}

bool firstMouse = true;

void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
  if (firstMouse) {
    lastX = static_cast<float>(xpos);
    lastY = static_cast<float>(ypos);
    firstMouse = false;
  }

  float xoffset = xpos - lastX;
  float yoffset = lastY - ypos;
  lastX = xpos;
  lastY = ypos;

  float sensitivity = 0.05f;
  xoffset *= sensitivity;
  yoffset *= sensitivity;

  yaw += xoffset;
  pitch += yoffset;

  if (pitch > 89.0f)
    pitch = 89.0f;
  if (pitch < -89.0f)
    pitch = -89.0f;

  glm::vec3 front;
  front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
  front.y = sin(glm::radians(pitch));
  front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
  cameraFront = glm::normalize(front);
}

unsigned createProgram(const char* VSSource, const char* FSSource) {
  unsigned VS, FS, program;
  int status;
  char infoLog[512];

  VS = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(VS, 1, &VSSource, nullptr);
  glCompileShader(VS);

  glGetShaderiv(VS, GL_COMPILE_STATUS, &status);
  if (!status) {
    glGetShaderInfoLog(VS, 512, NULL, infoLog);
    std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n"
              << infoLog << std::endl;
  }

  FS = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(FS, 1, &FSSource, nullptr);
  glCompileShader(FS);

  glGetShaderiv(FS, GL_COMPILE_STATUS, &status);
  if (!status) {
    glGetShaderInfoLog(FS, 512, NULL, infoLog);
    std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n"
              << infoLog << std::endl;
  }

  program = glCreateProgram();
  glAttachShader(program, VS);
  glAttachShader(program, FS);
  glLinkProgram(program);

  glGetProgramiv(program, GL_LINK_STATUS, &status);
  if (!status) {
    glGetProgramInfoLog(program, 512, NULL, infoLog);
    std::cout << "ERROR::PROGRAM::COMPILATION_FAILED\n" << infoLog << std::endl;
  }

  glDeleteShader(VS);
  glDeleteShader(FS);

  return program;
}

// renderQuad() renders a 1x1 XY quad in NDC
// -----------------------------------------
unsigned int quadVAO = 0;
unsigned int quadVBO;
void renderQuad() {
  if (quadVAO == 0) {
    float quadVertices[] = {
        // positions        // texture Coords
        -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
        1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 1.0f,  -1.0f, 0.0f, 1.0f, 0.0f,
    };
    // setup plane VAO
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices,
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void*)(3 * sizeof(float)));
  }
  glBindVertexArray(quadVAO);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);
}

// renderCube() renders a 1x1 3D cube in NDC.
// -------------------------------------------------
unsigned int cubeVAO = 0;
unsigned int cubeVBO = 0;
void renderCube() {
  // initialize (if necessary)
  if (cubeVAO == 0) {
    float vertices[] = {
        // back face
        -1.0f, -1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f,  // bottom-left
        1.0f, 1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f,    // top-right
        1.0f, -1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f,   // bottom-right
        1.0f, 1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f,    // top-right
        -1.0f, -1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f,  // bottom-left
        -1.0f, 1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f,   // top-left
        // front face
        -1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,  // bottom-left
        1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f,   // bottom-right
        1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,    // top-right
        1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,    // top-right
        -1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,   // top-left
        -1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,  // bottom-left
        // left face
        -1.0f, 1.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f,    // top-right
        -1.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f,   // top-left
        -1.0f, -1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f,  // bottom-left
        -1.0f, -1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f,  // bottom-left
        -1.0f, -1.0f, 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f,   // bottom-right
        -1.0f, 1.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f,    // top-right
                                                             // right face
        1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,      // top-left
        1.0f, -1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,    // bottom-right
        1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,     // top-right
        1.0f, -1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,    // bottom-right
        1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,      // top-left
        1.0f, -1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,     // bottom-left
        // bottom face
        -1.0f, -1.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f,  // top-right
        1.0f, -1.0f, -1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f,   // top-left
        1.0f, -1.0f, 1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,    // bottom-left
        1.0f, -1.0f, 1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,    // bottom-left
        -1.0f, -1.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f,   // bottom-right
        -1.0f, -1.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f,  // top-right
        // top face
        -1.0f, 1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,  // top-left
        1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,    // bottom-right
        1.0f, 1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,   // top-right
        1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,    // bottom-right
        -1.0f, 1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,  // top-left
        -1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f    // bottom-left
    };
    glGenVertexArrays(1, &cubeVAO);
    glGenBuffers(1, &cubeVBO);
    // fill buffer
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    // link vertex attributes
    glBindVertexArray(cubeVAO);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
  }
  // render Cube
  glBindVertexArray(cubeVAO);
  glDrawArrays(GL_TRIANGLES, 0, 36);
  glBindVertexArray(0);
}