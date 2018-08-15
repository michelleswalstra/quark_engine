#pragma once

#define VK_USE_PLATFORM_WIN32_KHR 1

#include <vulkan/vulkan.hpp>

#include <qwindow.h>
#include <vector>
#include <glm/glm.hpp>

#include "EditObject.h"


class QResizeEvent;
class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QEvent;
class QTimer;

namespace qe {
    namespace core {
        class Mesh;
        class Transform;
        class QuarkObject;
        class Scene;
        class Shader;
        class Resource;
        class Material;
        class MeshFilter;
        class MeshRenderer;
        class Shader;
        class ScriptBehaviour;
    }

    namespace render {
        namespace vulkan {
            class VulkanDevice;
            class VulkanBuffer;
        }
    }

    namespace edit {
        class CameraController;

        class QuarkWindow : public QWindow, public EditObject
        {
            Q_OBJECT
        public:
            struct Vertex {
                glm::vec3 pos;
                glm::vec3 normal;
                glm::vec2 uv;

                static vk::VertexInputBindingDescription getBindingDescription() {
                    vk::VertexInputBindingDescription bindingDescription = {};
                    bindingDescription.binding = 0;
                    bindingDescription.stride = sizeof(Vertex);
                    bindingDescription.inputRate = vk::VertexInputRate::eVertex;

                    return bindingDescription;
                }

                static std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescriptions() {
                    std::array<vk::VertexInputAttributeDescription, 3> attributeDescriptions = {};

                    attributeDescriptions[0].binding = 0;
                    attributeDescriptions[0].location = 0;
                    attributeDescriptions[0].format = vk::Format::eR32G32B32Sfloat;
                    attributeDescriptions[0].offset = offsetof(Vertex, pos);

                    attributeDescriptions[1].binding = 0;
                    attributeDescriptions[1].location = 1;
                    attributeDescriptions[1].format = vk::Format::eR32G32B32Sfloat;
                    attributeDescriptions[1].offset = offsetof(Vertex, normal);

                    attributeDescriptions[2].binding = 0;
                    attributeDescriptions[2].location = 2;
                    attributeDescriptions[2].format = vk::Format::eR32G32B32Sfloat;
                    attributeDescriptions[2].offset = offsetof(Vertex, uv);

                    return attributeDescriptions;
                }

                bool operator==(const Vertex& other) const {
                    return pos == other.pos && normal == other.normal && uv == other.uv;
                }
            };

            struct UniformCameraBuffer {
                glm::mat4 view;
                glm::mat4 proj;
            };

            struct UniformLightBuffer {
                glm::vec4 lightDir;
            };

            struct ModelMatrix {
                glm::mat4 model;
            };

        public:
            explicit QuarkWindow();
            ~QuarkWindow();

            void Init();

            void Awake();

            void UpdateBehaviour();

            void Draw();

			void LoadScene(const std::string& scene_path);

			void ReleaseScene();


            inline auto Fps()const -> const int& { return fps_number_; }

        public slots:
            void update();


        protected:
            void resizeEvent(QResizeEvent *event) override;
            void mouseMoveEvent(QMouseEvent *event) override;
            void mousePressEvent(QMouseEvent *event) override;
            void mouseReleaseEvent(QMouseEvent *event) override;
            void wheelEvent(QWheelEvent *event) override;
            void keyPressEvent(QKeyEvent *event) override;
            void keyReleaseEvent(QKeyEvent *event) override;

            bool event(QEvent *ev) override;


        private:
            void DrawScene(std::shared_ptr<qe::core::Scene> scene);

            void LoadQuarkObject(
                std::shared_ptr<qe::core::QuarkObject> quark_object,
                std::shared_ptr<qe::core::Material> standard_material);

            void LoadDrawData(
                glm::mat4 m,
                std::shared_ptr<qe::core::MeshFilter> mesh_filter,
                std::shared_ptr<qe::core::MeshRenderer> mesh_renderer);

            void GetMeshData(std::shared_ptr<qe::core::Mesh> mesh, std::vector<Vertex>& vertexs, std::vector<uint>& indexs);

            void CreateDescriptorSetLayout();

            void CreatePipeline();

            void CreateUniformBuffer();

            void CreateDescriptorSet();

            void CreateCommandBuffer();

            void UpdateUniformBuffer();

            void RecreateSwapChain();

            void CleanSwapChain();

			void ReleaseSceneData();

			void ReleaseRenderData();

            void InitCamera(const int& width, const int& height);
       
        private:
            std::shared_ptr<qe::render::vulkan::VulkanDevice> vi_device_;
            std::shared_ptr<qe::core::Resource> resource_;
            std::shared_ptr<qe::edit::CameraController> camera_controller_;

            vk::Buffer ubo_buffer_;
            vk::DeviceMemory ubo_buffer_memory_;

            vk::Buffer uld_buffer_;
            vk::DeviceMemory uld_buffer_memory_;

            vk::DescriptorSetLayout descriptor_set_layout_;

            vk::DescriptorPool descriptor_set_pool_;

            vk::DescriptorSet descriptor_set_;

            vk::PipelineLayout pipeline_layout_;

            vk::Pipeline pipeline_;

            std::vector<vk::CommandBuffer> command_buffers_;

            struct meshData {
                std::vector<Vertex> vertexs;
                std::vector<uint> indexs;
                std::shared_ptr<qe::render::vulkan::VulkanBuffer> vertex_buffer;
                std::shared_ptr<qe::render::vulkan::VulkanBuffer> index_buffer;
                glm::mat4 m;
            };

            std::vector<meshData> mesh_datas_;

            std::vector<std::shared_ptr<qe::core::ScriptBehaviour>>  behaviours_;

            std::shared_ptr<QTimer> graphics_timer_;

            std::shared_ptr<qe::core::Scene> scene_;

            std::string kShaderPath;

            std::string kShaderPre;

            glm::vec4 light_dir_;

            int frame_count_;
            int fps_number_;

            float fps_time_;
            float delta_time_;
            float key_press_time_;

            bool is_update_material_;
            bool right_button_press_;
            bool middle_button_press_;
            bool key_press_;
            bool init_mouse_pos_;


            glm::vec2 mouse_last_pos_;

            std::chrono::time_point<std::chrono::steady_clock> press_time_;

        };
    }
}
