#include <GL/glew.h>
#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <skeletal_mesh.h>

#include <string>
#include <iostream>
#include <random>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

constexpr int SCREEN_WIDTH = 800;
constexpr int SCREEN_HEIGHT = 600;

const char* geometryVS =
    "#version 450\n"
    "layout (location = 0) in vec3 aPos;\n"
    "layout (location = 1) in vec2 aTexCoords;\n"
    "layout (location = 2) in vec3 aNormal;\n"
    "layout (location = 3) in ivec4 aBoneIndex;\n"
    "layout (location = 4) in vec4 aBoneWeight;\n"
    "out vec3 FragPos;\n"
    "out vec2 TexCoords;\n"
    "out vec3 Normal;\n"
    "uniform mat4 model;\n"
    "uniform mat4 view;\n"
    "uniform mat4 projection;\n"
    "void main() {\n"
    "    vec4 viewPos = view * model * vec4(aPos, 1.0);\n"
    "    FragPos = viewPos.xyz;\n"
    "    TexCoords = aTexCoords;\n"
    "    Normal = transpose(inverse(mat3(view * model))) * aNormal;\n"
    "    gl_Position = projection * viewPos;\n"
    "}\n";

const char* geometryFS =
    "#version 450\n"
    "in vec3 FragPos;\n"
    "in vec2 TexCoords;\n"
    "in vec3 Normal;\n"
    "out vec3 gPosition;\n"
    "out vec3 gNormal;\n"
    "out vec3 gAlbedo;\n"
    "void main() {\n"
    "    gPosition = FragPos;\n"
    "    gNormal = normalize(Normal);\n"
    "    gAlbedo.rgb = vec3(0.95);\n"
    "}\n";

const char* ssaoVS =
    "#version 450\n"
    "layout (location = 0) in vec3 aPos;\n"
    "layout (location = 1) in vec2 aTexCoords;\n"
    "out vec2 TexCoords;\n"
    "void main() {\n"
    "    TexCoords = aTexCoords;\n"
    "    gl_Position = vec4(aPos, 1.0);\n"
    "}\n";

const char* ssaoFS =
    "#version 450\n"
    "uniform sampler2D gPosition;\n"
    "uniform sampler2D gNormal;\n"
    "uniform vec3 kernel[64];\n"
    "uniform mat4 projection;\n"
    "const float radius = 0.05;\n"
    "in vec2 TexCoords;\n"
    "out vec3 FragColor;\n"
    "void main() {\n"
    "    vec3 fragPos = texture(gPosition, TexCoords).xyz;\n"
    "    vec3 normal = normalize(texture(gNormal, TexCoords).rgb);\n"
    "    float occlusion = 0.0;\n"
    "    for (int i = 0; i < 64; i++) {\n"
    "        vec3 samplePos = fragPos + kernel[i] * radius;\n"
    "        vec4 screenPos = projection * vec4(samplePos, 1.0);\n"
    "        screenPos.xyz /= screenPos.w;\n"
    "        screenPos.xyz = screenPos.xyz * 0.5 + 0.5;\n"
    "        float sampleDepth = texture(gPosition, screenPos.xy).z;\n"
    "        occlusion += (sampleDepth >= samplePos.z ? 1.0 : 0.0);\n"
    "    }\n"
    "    FragColor = vec3(occlusion / 64);\n"
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

#define modelName "abacus"

int main() {
  // 初始化
  glfwInit();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
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

  // 导入模型
  SkeletalMesh::Scene& sr =
      SkeletalMesh::Scene::loadScene(modelName, "resources/" modelName ".fbx");
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
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  glEnable(GL_DEPTH_TEST);
  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

  glUseProgram(ssaoProgram);
  glUniform1i(glGetUniformLocation(ssaoProgram, "gPosition"), 0);
  glUniform1i(glGetUniformLocation(ssaoProgram, "gNormal"), 1);

  // 随机取样
  std::uniform_real_distribution<float> randomFloats(-1.0, 1.0);
  std::default_random_engine generator;
  std::vector<glm::vec3> ssaoKernel(64);
  for (int i = 0; i < 64; i++) {
    float x = randomFloats(generator);
    float y = randomFloats(generator);
    float z = randomFloats(generator);

    ssaoKernel[i] = normalize(glm::vec3(x, y, z));
  }

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

    // Geometry Pass
    glBindFramebuffer(GL_FRAMEBUFFER, gBuffer);
    glUseProgram(geometryProgram);
    //glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glm::mat4 model(1.0f), view(1.0f), projection(1.0f);
    model = glm::scale(model, glm::fvec3(0.03f));
    view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    projection = glm::perspective(glm::radians(fov), ratio,
                                  0.1f, 100.0f);
    glUniformMatrix4fv(glGetUniformLocation(geometryProgram, "model"), 1,
                       GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(glGetUniformLocation(geometryProgram, "view"), 1,
                       GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(geometryProgram, "projection"), 1,
                       GL_FALSE, glm::value_ptr(projection));
    sr.render();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // SSAO PASS
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
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
    //glActiveTexture(GL_TEXTURE2);
    //glBindTexture(GL_TEXTURE_2D, noiseTexture);
    renderQuad();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glfwSwapBuffers(window);
  }

  SkeletalMesh::Scene::unloadScene("abacus");
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

void doMovement(float timePeriod) {
  float cameraSpeed = 1.0f;
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