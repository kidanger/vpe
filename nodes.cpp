#ifndef IMGUI_DEFINE_MATH_OPERATORS
#   define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <vector>
#include <iostream>
#include <map>
#include <string>
#include <regex>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include "ImNodes.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <SDL.h>

#include "pipeline.hpp"

ImNodes::CanvasState* gCanvas = nullptr;
std::vector<struct BaseNode*> nodes;
static class RunContext* context;

static const char* PipeInputSlotNames[] = {
    "<1",
    "<2",
    "<3",
    "<4",
    "<5",
    "<6",
    "<7",
};

static const char* PipeOutputSlotNames[] = {
    ">1",
    ">2",
    ">3",
    ">4",
    ">5",
    ">6",
    ">7",
};

class RunContext
{
    int nextfifo = 0;
    std::map<std::tuple<const void*, const char*, const void*, const char*>, std::string> fifos;
    std::string dir = "tmp/";
    Pipeline pipeline;

public:

    std::string MakeFifo()
    {
        std::string filename = dir + "fifo" + std::to_string(nextfifo++);
        int ret = mkfifo(filename.c_str(), 0600);
        if (ret) {
            perror("mkfifo");
            return std::string();
        }
        printf("mkfifo %s\n", filename.c_str());
        return filename;
    }

    std::string MakeOrGetFifo(const void* node1, const char* slot1,
                              const void* node2, const char* slot2)
    {
        auto key = std::make_tuple(node1, slot1, node2, slot2);
        if (fifos.find(key) == fifos.end())
        {
            fifos[key] = MakeFifo();
        }
        return fifos[key];
    }

    void RemoveFifo(const std::string& filename)
    {
        printf("rm %s\n", filename.c_str());
    }

    void CollectBlock(Block* b)
    {
        pipeline.Add(b);
    }

    void Run();
};


struct Connection
{
    /// `id` that was passed to BeginNode() of input node.
    void* input_node = nullptr;
    /// Descriptor of input slot.
    const char* input_slot = nullptr;
    /// `id` that was passed to BeginNode() of output node.
    void* output_node = nullptr;
    /// Descriptor of output slot.
    const char* output_slot = nullptr;

    bool operator==(const Connection& other) const
    {
        return input_node == other.input_node &&
               input_slot == other.input_slot &&
               output_node == other.output_node &&
               output_slot == other.output_slot;
    }

    bool operator!=(const Connection& other) const
    {
        return !operator ==(other);
    }

    bool Prepare(RunContext& ctx)
    {
        return !GetFifoName(ctx).empty();
    }

    std::string GetFifoName(RunContext& ctx)
    {
        return ctx.MakeOrGetFifo(input_node, input_slot, output_node, output_slot);
    }
};

enum NodeSlotTypes
{
    NodeSlotPosition = 1,   // ID can not be 0
    NodeSlotRotation,
    NodeSlotMatrix,
    NodeSlotPipe,
};

struct BaseNode
{
    /// Title which will be displayed at the center-top of the node.
    const char* title = nullptr;
    /// Flag indicating that node is selected by the user.
    bool selected = false;
    /// Node position on the canvas.
    ImVec2 pos{};
    /// List of node connections.
    std::vector<Connection> connections{};
    /// Last recorded width of the node. Used to center node title.
    float node_width = 0.f;
    /// Max width of output node title. Used to align output nodes to the right edge.
    float output_max_title_width = 0.f;

    explicit BaseNode(const char* title)
    {
        this->title = title;
    }
    virtual ~BaseNode() = default;

    BaseNode(const BaseNode& other) = delete;

    /// Renders a single node and it's connections.
    void RenderNode()
    {
        // Start rendering node
        if (ImNodes::BeginNode(this, &pos, &selected))
        {
            // Render node title
            RenderTitle();

            // Render node-specific slots
            RenderNodeSlots();

            // Store new connections when they are created
            Connection new_connection;
            if (ImNodes::GetNewConnection(&new_connection.input_node, &new_connection.input_slot,
                &new_connection.output_node, &new_connection.output_slot))
            {
                // remove potential previous connection to the input slot
                for (auto& c : ((BaseNode*) new_connection.input_node)->connections)
                {
                    if (c.input_slot == new_connection.input_slot)
                    {
                        ((BaseNode*) c.input_node)->DeleteConnection(c);
                        ((BaseNode*) c.output_node)->DeleteConnection(c);
                    }
                }
                ((BaseNode*) new_connection.input_node)->connections.push_back(new_connection);
                ((BaseNode*) new_connection.output_node)->connections.push_back(new_connection);
            }

            // Render output connections of this node
            for (const Connection& connection : connections)
            {
                // Node contains all it's connections (both from output and to input slots). This means that multiple
                // nodes will have same connection. We render only output connections and ensure that each connection
                // will be rendered once.
                if (connection.output_node != this)
                    continue;

                if (!ImNodes::Connection(connection.input_node, connection.input_slot, connection.output_node,
                    connection.output_slot))
                {
                    // Remove deleted connections
                    ((BaseNode*) connection.input_node)->DeleteConnection(connection);
                    ((BaseNode*) connection.output_node)->DeleteConnection(connection);
                }
            }

            // Node rendering is done. This call will render node background based on size of content inside node.
            ImNodes::EndNode();

            // Store node width which is needed for centering title.
            node_width = ImGui::GetItemRectSize().x;
        }
    }

    /// Renders custom node content (slots, widgets)
    virtual void RenderNodeSlots() = 0;

    virtual void RenderTitle()
    {
        if (node_width > 0)
        {
            // Center node title
            ImVec2 title_size = ImGui::CalcTextSize(title);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + node_width / 2.f - title_size.x / 2.f);
        }

        ImGui::TextUnformatted(title);
    }

    /// Renders a single slot, handles positioning, colors, icon, title.
    void RenderSlot(const char* slot_title, int kind)
    {
        const auto& style = ImGui::GetStyle();
        const float CIRCLE_RADIUS = 5.f * gCanvas->zoom;
        ImVec2 title_size = ImGui::CalcTextSize(slot_title);
        // Pull entire slot a little bit out of the edge so that curves connect into int without visible seams
        float item_offset_x = style.ItemSpacing.x * gCanvas->zoom;
        if (!ImNodes::IsOutputSlotKind(kind))
            item_offset_x = -item_offset_x;
        ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2{item_offset_x, 0});

        if (ImNodes::BeginSlot(slot_title, kind))
        {
            auto* draw_lists = ImGui::GetWindowDrawList();

            // Slot appearance can be altered depending on curve hovering state.
            bool is_active = ImNodes::IsSlotCurveHovered() ||
                (ImNodes::IsConnectingCompatibleSlot() && !IsAlreadyConnectedWithPendingConnection(slot_title, kind));

            ImColor color = gCanvas->colors[is_active ? ImNodes::ColConnectionActive : ImNodes::ColConnection];

            ImGui::PushStyleColor(ImGuiCol_Text, color.Value);

            if (ImNodes::IsOutputSlotKind(kind))
            {
                // Align output slots to the right edge of the node.
                output_max_title_width = ImMax(output_max_title_width, title_size.x);
                float offset = (output_max_title_width + style.ItemSpacing.x) - title_size.x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);

                ImGui::TextUnformatted(slot_title);
                ImGui::SameLine();
            }

            ImRect circle_rect{
                ImGui::GetCursorScreenPos(),
                ImGui::GetCursorScreenPos() + ImVec2{CIRCLE_RADIUS * 2, CIRCLE_RADIUS * 2}
            };
            // Vertical-align circle in the middle of the line.
            float circle_offset_y = title_size.y / 2.f - CIRCLE_RADIUS;
            circle_rect.Min.y += circle_offset_y;
            circle_rect.Max.y += circle_offset_y;
            draw_lists->AddCircleFilled(circle_rect.GetCenter(), CIRCLE_RADIUS, color);

            ImGui::ItemSize(circle_rect.GetSize());
            ImGui::ItemAdd(circle_rect, ImGui::GetID(title));

            if (ImNodes::IsInputSlotKind(kind))
            {
                ImGui::SameLine();
                ImGui::TextUnformatted(slot_title);
            }

            ImGui::PopStyleColor();
            ImNodes::EndSlot();

            // A dirty trick to place output slot circle on the border.
            ImGui::GetCurrentWindow()->DC.CursorMaxPos.x -= item_offset_x;
        }
    }

    /// Extra convenience - do not highlight slot which is already connected to current source slot of pending new connection.
    bool IsAlreadyConnectedWithPendingConnection(const char* current_slot_title, int current_slot_kind)
    {
        void* other_node_id;
        const char* other_slot_title;
        int other_slot_kind;
        if (ImNodes::GetPendingConnection(&other_node_id, &other_slot_title, &other_slot_kind))
        {
            assert(ImNodes::IsInputSlotKind(current_slot_kind) != ImNodes::IsInputSlotKind(other_slot_kind));

            if (ImNodes::IsInputSlotKind(other_slot_kind))
            {
                for (const auto& conn : connections)
                {
                    if (conn.input_node == other_node_id && strcmp(conn.input_slot, other_slot_title) == 0 &&
                        conn.output_node == this && strcmp(conn.output_slot, current_slot_title) == 0)
                        return true;
                }
            }
            else
            {
                for (const auto& conn : connections)
                {
                    if (conn.input_node == this && strcmp(conn.input_slot, current_slot_title) == 0 &&
                        conn.output_node == other_node_id && strcmp(conn.output_slot, other_slot_title) == 0)
                        return true;
                }
            }
        }
        return false;
    }

    /// Deletes connection from this node.
    void DeleteConnection(const Connection& connection)
    {
        for (auto it = connections.begin(); it != connections.end(); ++it)
        {
            if (connection == *it)
            {
                connections.erase(it);
                break;
            }
        }
    }

    virtual bool Prepare(RunContext& ctx) {
        return true;
    }
};

struct VPPOperator : BaseNode
{
    explicit VPPOperator() : BaseNode("vpp operator") { }

    std::string command;
    int ninputs = 0;
    int noutputs = 0;
    bool checked = false;

    CommandBlock* block = nullptr;

    void RenderNodeSlots() override
    {
        const auto& style = ImGui::GetStyle();

        ImGui::BeginGroup();
        {
            for (int i = 0; i < ninputs; i++) {
                RenderSlot(PipeInputSlotNames[i], ImNodes::InputSlotKind(NodeSlotPipe));
            }
        }
        ImGui::EndGroup();

        ImGui::SetCursorScreenPos({ImGui::GetItemRectMax().x + style.ItemSpacing.x, ImGui::GetItemRectMin().y});
        ImGui::BeginGroup();
        if (block && block->IsRunning())
        {
            ImGui::TextUnformatted("running");
            if (ImGui::Button("stop")) {
                block->Stop();
            }
        }
        else
        {
            ImGui::TextUnformatted("not running");
        }
        if (ImGui::Button("console")) {
            ImGui::OpenPopup("Console");
        }
        if (noutputs == 0 && ImGui::Button("run")) {
            context->Run();
        }
        if (ImGui::BeginPopup("Console")) {
            if (block)
                ImGui::TextUnformatted(block->GetOutput().c_str());
            if (ImGui::IsAnyMouseDown() && !ImGui::IsWindowHovered())
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::EndGroup();

        ImGui::SetCursorScreenPos({ImGui::GetItemRectMax().x + style.ItemSpacing.x, ImGui::GetItemRectMin().y});
        ImGui::BeginGroup();
        {
            for (int i = 0; i < noutputs; i++) {
                RenderSlot(PipeOutputSlotNames[i], ImNodes::OutputSlotKind(NodeSlotPipe));
            }
        }
        ImGui::EndGroup();
    }

    virtual void RenderTitle() override
    {
        ImGui::SetNextItemWidth(150);

        float title_size = ImGui::CalcItemWidth();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + node_width / 2.f - title_size / 2.f);

        if (ImGui::InputText("##command", &command, ImGuiInputTextFlags_EnterReturnsTrue)) {
            SetCommand(command);
        }
    }

    void SetCommand(const std::string& str)
    {
        command = str;
        ninputs = 0;
        while (command.find(PipeInputSlotNames[ninputs]) != std::string::npos) {
            ninputs++;
        }
        noutputs = 0;
        while (command.find(PipeOutputSlotNames[noutputs]) != std::string::npos) {
            noutputs++;
        }
    }

    virtual bool Prepare(RunContext& ctx) override
    {
        std::string command = this->command;
        for (int i = 0; i < ninputs; i++) {
            Connection* con = nullptr;
            for (auto& c : connections)
            {
                if (c.input_node != this || c.input_slot != PipeInputSlotNames[i])
                    continue;
                con = &c;
            }
            if (!con) {
                printf("input slot %d not connected?\n", i);
                return false;
            }
            const std::string& name = con->GetFifoName(ctx);
            if (name.empty()) {
                printf("slot %d not prepared\n", i);
                return false;
            }
            command = std::regex_replace(command, std::regex(PipeInputSlotNames[i]), name);
        }
        // TODO: duplicate output if multiple inputs use it
        for (int i = 0; i < noutputs; i++) {
            std::vector<Connection*> outputs;
            for (auto& c : connections)
            {
                if (c.output_node != this || c.output_slot != PipeOutputSlotNames[i])
                    continue;
                outputs.push_back(&c);
            }
            if (outputs.empty()) {
                printf("output slot %d not connected?\n", i);
                return false;
            }
            std::string fifoname;
            if (outputs.size() == 1)
            {
                fifoname = outputs[0]->GetFifoName(ctx);
            }
            else
            {
                // multiple outputs, so we need to duplicate the stream
                // XXX: this is vpp specific
                auto fifo = ctx.MakeFifo();
                fifoname = fifo;
                for (unsigned i = 0; i < outputs.size() - 1; i++)
                {
                    std::string from = fifo;
                    std::string to;
                    if (i == outputs.size() - 2)
                    {
                        to = outputs[i+1]->GetFifoName(ctx);
                    }
                    else
                    {
                        fifo = ctx.MakeFifo();
                        to = fifo;
                    }
                    std::string to2 = outputs[i]->GetFifoName(ctx);
                    ctx.CollectBlock(new CommandBlock("vp dup " + from + " " + to + " " + to2));
                }
            }
            if (fifoname.empty()) {
                printf("slot %d not prepared\n", i);
                return false;
            }
            command = std::regex_replace(command, std::regex(PipeOutputSlotNames[i]), fifoname);
        }

        block = new CommandBlock(command);
        ctx.CollectBlock(block);
        return true;
    }
};

void RunContext::Run()
{
    pipeline.Clear();

    for (auto node : nodes)
    {
        for (auto& c : node->connections)
        {
            if (c.output_node != node)
                continue;
            if (!c.Prepare(*this))
            {
                return;
            }
        }
    }

    for (auto node : nodes)
    {
        if (!node->Prepare(*this))
        {
            return;
        }
    }

    pipeline.Launch();
}

std::map<std::string, BaseNode*(*)()> available_nodes{
    {"vpp operator", []() -> BaseNode* {
                                           auto x = new VPPOperator();
                                           x->SetCommand("ls <1 >1 >2");
                                           return x;
                                       }},
    {"Video Input", []() -> BaseNode* {
                                           auto x = new VPPOperator();
                                           x->SetCommand("vid2vpp file.avi >1");
                                           return x;
                                      }},
    {"Video Output", []() -> BaseNode* {
                                           auto x = new VPPOperator();
                                           x->SetCommand("vpp2vid <1 file.avi");
                                           return x;
                                      }},

};

void vpe_show()
{
    bool _new = false;
    if (gCanvas == nullptr)
    {
        // Canvas must be created after ImGui initializes, because constructor accesses ImGui style to configure default
        // colors.
        gCanvas = new ImNodes::CanvasState();
        _new = true;
    }

    if (!context)
    {
        context = new RunContext();
    }

    if (ImGui::Begin("ImNodes", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
                     | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
    {
        // We probably need to keep some state, like positions of nodes/slots for rendering connections.
        ImNodes::BeginCanvas(gCanvas);
        for (auto it = nodes.begin(); it != nodes.end();)
        {
            BaseNode* node = *it;
            (*it)->RenderNode();

            if (node->selected && ImGui::IsKeyPressedMap(ImGuiKey_Delete))
            {
                for (auto& connection : node->connections)
                {
                    ((BaseNode*) connection.input_node)->DeleteConnection(connection);
                    ((BaseNode*) connection.output_node)->DeleteConnection(connection);
                }
                delete node;
                it = nodes.erase(it);
            }
            else
                ++it;
        }

        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsMouseReleased(1) && ImGui::IsWindowHovered() && !ImGui::IsMouseDragging(1))
        {
            ImGui::FocusWindow(ImGui::GetCurrentWindow());
            ImGui::OpenPopup("NodesContextMenu");
        }

        if (_new) {
            auto read = new VPPOperator();
            //read->SetCommand("vid2vpp input.avi >1");
            read->SetCommand("webcam2vpp >1");
            read->pos = ImVec2(50, 320);
            nodes.push_back(read);
            auto skip = new VPPOperator();
            skip->SetCommand("vp map <1 >1 \"(x/255)^2*255\"");
            skip->pos = ImVec2(250, 320);
            nodes.push_back(skip);
            auto write = new VPPOperator();
            //write->SetCommand("vpp2vid <1 output.avi; echo done");
            write->SetCommand("vpp2win <1");
            //write->SetCommand("vp timeinterval <1");
            write->pos = ImVec2(350, 420);
            nodes.push_back(write);

            Connection c1;
            c1.input_node = skip;
            c1.input_slot = PipeInputSlotNames[0];
            c1.output_node = read;
            c1.output_slot = PipeOutputSlotNames[0];
            skip->connections.push_back(c1);
            read->connections.push_back(c1);

            Connection c2;
            c2.input_node = write;
            c2.input_slot = PipeInputSlotNames[0];
            c2.output_node = skip;
            c2.output_slot = PipeOutputSlotNames[0];
            skip->connections.push_back(c2);
            write->connections.push_back(c2);
        }

        if (ImGui::BeginPopup("NodesContextMenu"))
        {
            for (const auto& desc : available_nodes)
            {
                if (ImGui::MenuItem(desc.first.c_str()))
                {
                    nodes.push_back(desc.second());
                    ImNodes::AutoPositionNode(nodes.back());
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Zoom"))
                gCanvas->zoom = 1;

            if (ImGui::IsAnyMouseDown() && !ImGui::IsWindowHovered())
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (!io.WantCaptureKeyboard && ImGui::IsKeyPressed(SDL_SCANCODE_P)) {
            context->Run();
        }

        ImNodes::EndCanvas();
    }
    ImGui::End();
}

