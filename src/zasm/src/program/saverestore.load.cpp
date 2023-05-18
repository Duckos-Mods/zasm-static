#include "zasm/program/saverestore.hpp"

#include "program.node.hpp"
#include "program.state.hpp"
#include "saverestorehelper.hpp"
#include "saverestoretypes.hpp"
#include "zasm/program/program.hpp"

#include <fstream>
#include <zasm/core/filestream.hpp>

namespace zasm
{
    template<typename T> Node* loadNode_(SaveRestore& helper, Program& program);

    template<> static Node* loadNode_<Sentinel>(SaveRestore& helper, Program& program)
    {
        return program.createNode(Sentinel{});
    }

    template<> static Node* loadNode_<Label>(SaveRestore& helper, Program& program)
    {
        Label::Id id{};
        helper >> id;

        return program.createNode(Label(id));
    }

    template<> static Node* loadNode_<EmbeddedLabel>(SaveRestore& helper, Program& program)
    {
        Label::Id labelId{};
        Label::Id relLabelId{};
        BitSize size{};

        helper >> labelId;
        helper >> relLabelId;
        helper >> size;

        return program.createNode(EmbeddedLabel(Label(labelId), Label(relLabelId), size));
    }

    template<> static Node* loadNode_<Data>(SaveRestore& helper, Program& program)
    {
        std::uint64_t size{};
        std::uint64_t repeats{};

        helper >> size;
        helper >> repeats;

        std::vector<std::uint8_t> buf;
        buf.resize(size);
        if (auto err = helper.read(buf.data(), size); err != Error::None)
        {
            return nullptr;
        }

        Data data(buf.data(), size);
        data.setRepeatCount(repeats);

        return program.createNode(std::move(data));
    }

    template<> static Node* loadNode_<Section>(SaveRestore& helper, Program& program)
    {
        Section::Id id{};
        helper >> id;

        return program.createNode(Section(id));
    }

    template<> static Node* loadNode_<Align>(SaveRestore& helper, Program& program)
    {
        std::uint32_t alignVal{};
        Align::Type alignType{};

        helper >> alignType;
        helper >> alignVal;

        return program.createNode(Align(alignType, alignVal));
    }

    template<typename T> static Error loadOperand_(SaveRestore& helper, Operand& op);

    template<> static Error loadOperand_<Operand::None>(SaveRestore& helper, Operand&)
    {
        return Error::None;
    }

    template<> static Error loadOperand_<Reg>(SaveRestore& helper, Operand& op)
    {
        Reg::Id regId{};
        helper >> regId;

        op = Reg(regId);

        return Error::None;
    }

    template<> static Error loadOperand_<Mem>(SaveRestore& helper, Operand& op)
    {
        BitSize bitSize{};
        Reg::Id regSeg{};
        Reg::Id regBase{};
        Reg::Id regIdx{};
        std::uint8_t scale{};
        std::int64_t disp{};
        Label::Id labelId{};

        helper >> bitSize;
        helper >> regSeg;
        helper >> regBase;
        helper >> regIdx;
        helper >> scale;
        helper >> disp;
        helper >> labelId;

        op = Mem(bitSize, Reg(regSeg), Label(labelId), Reg(regBase), Reg(regIdx), scale, disp);

        return Error::None;
    }

    template<> static Error loadOperand_<Imm>(SaveRestore& helper, Operand& op)
    {
        std::int64_t val{};
        helper >> val;

        op = Imm(val);

        return Error::None;
    }

    template<> static Error loadOperand_<Label>(SaveRestore& helper, Operand& op)
    {
        Label::Id labelId{};
        helper >> labelId;

        op = Label(labelId);

        return Error::None;
    }

    template<> static Node* loadNode_<Instruction>(SaveRestore& helper, Program& program)
    {
        Instruction::Mnemonic::ValueType mnemonic{};
        Instruction::Attribs::ValueType attribs{};
        std::uint8_t opCount{};

        helper >> mnemonic;
        helper >> attribs;
        helper >> opCount;

        Instruction instr(mnemonic);
        instr.addAttribs(attribs);

        for (std::uint8_t i = 0; i < opCount; ++i)
        {
            OperandType opType{};
            helper >> opType;

            Operand op{};
            switch (opType)
            {
                case OperandType::None:
                    if (auto err = loadOperand_<Operand::None>(helper, op); err != Error::None)
                    {
                        return nullptr;
                    }
                    break;
                case OperandType::Register:
                    if (auto err = loadOperand_<Reg>(helper, op); err != Error::None)
                    {
                        return nullptr;
                    }
                    break;
                case OperandType::Memory:
                    if (auto err = loadOperand_<Mem>(helper, op); err != Error::None)
                    {
                        return nullptr;
                    }
                    break;
                case OperandType::Immediate:
                    if (auto err = loadOperand_<Imm>(helper, op); err != Error::None)
                    {
                        return nullptr;
                    }
                    break;
                case OperandType::Label:
                    if (auto err = loadOperand_<Label>(helper, op); err != Error::None)
                    {
                        return nullptr;
                    }
                    break;
                default:
                    assert(false);
                    break;
            }

            instr.addOperand(op);
        }

        return program.createNode(std::move(instr));
    }

    static Error loadNode(SaveRestore& helper, Program& program)
    {
        Node::Id nodeId{};
        helper >> nodeId;

        std::uint64_t userData{};
        helper >> userData;

        NodeType nodeType{};
        helper >> nodeType;

        Node* node = nullptr;
        switch (nodeType)
        {
            case NodeType::Sentinel:
                node = loadNode_<Sentinel>(helper, program);
                break;
            case NodeType::Label:
                node = loadNode_<Label>(helper, program);
                break;
            case NodeType::EmbeddedLabel:
                node = loadNode_<EmbeddedLabel>(helper, program);
                break;
            case NodeType::Data:
                node = loadNode_<Data>(helper, program);
                break;
            case NodeType::Section:
                node = loadNode_<Section>(helper, program);
                break;
            case NodeType::Align:
                node = loadNode_<Align>(helper, program);
                break;
            case NodeType::Instruction:
                node = loadNode_<Instruction>(helper, program);
                break;
            default:
                assert(false);
                break;
        }

        if (node == nullptr)
        {
            return Error::InvalidParameter;
        }

        auto* intl = static_cast<detail::Node*>(node);
        intl->setId(nodeId);
        intl->setUserData(userData);

        program.append(node);

        return Error::None;
    }

    static Error loadNodes(SaveRestore& helper, Program& program)
    {
        std::uint64_t nodeCount{};

        helper >> nodeCount;
        for (std::uint64_t i = 0; i < nodeCount; ++i)
        {
            if (auto err = loadNode(helper, program); err != Error::None)
            {
                return err;
            }
        }

        return Error::None;
    }

    static Error loadLabels(SaveRestore& helper, Program& program)
    {
        auto& programState = program.getState();
        auto& labels = programState.labels;

        std::uint64_t labelCount{};
        helper >> labelCount;

        for (std::uint64_t i = 0; i < labelCount; ++i)
        {
            detail::LabelData labelData;
            helper >> labelData.flags;
            helper >> labelData.id;
            helper >> labelData.moduleId;
            helper >> labelData.nameId;

            Node::Id nodeId;
            helper >> nodeId;

            labels.push_back(labelData);
        }

        return Error::None;
    }

    static Error loadSections(SaveRestore& helper, Program& program)
    {
        auto& programState = program.getState();
        auto& sections = programState.sections;
        
        std::uint64_t sectionCount{};

        helper >> sectionCount;
        for (std::uint64_t i = 0; i < sectionCount; ++i)
        {
            detail::SectionData sectData;
            helper >> sectData.align;
            helper >> sectData.attribs;
            helper >> sectData.id;
            helper >> sectData.nameId;
            
            Node::Id nodeId;
            helper >> nodeId;

            sections.push_back(sectData);
        }

        return Error::None;
    }

    static Error loadSymbols(SaveRestore& helper, Program& program)
    {
        const auto& programState = program.getState();

        return Error::None;
    }

    static Error loadProgram(SaveRestore& helper, Program& program)
    {
        std::uint32_t signature{};

        if (auto err = helper.read(&signature, sizeof(signature)); err != Error::None)
        {
            return err;
        }

        if (signature != kZasmSignature)
        {
            return Error::SignatureMismatch;
        }

        MachineMode mode{};
        helper >> mode;

        program.setMode(mode);

        Node::Id nextNodeId{};
        helper >> nextNodeId;

        if (auto err = loadNodes(helper, program); err != Error::None)
        {
            return err;
        }

        if (auto err = loadLabels(helper, program); err != Error::None)
        {
            return err;
        }

        if (auto err = loadSections(helper, program); err != Error::None)
        {
            return err;
        }

        if (auto err = loadSymbols(helper, program); err != Error::None)
        {
            return err;
        }

        // Fix the internal state.
        auto& programState = program.getState();
        programState.nextNodeId = nextNodeId;

        return Error::None;
    }

    zasm::Expected<Program, Error> load(IStream& stream)
    {
        try
        {
            SaveRestore helper(stream, true);

            Program program{};
            if (auto err = loadProgram(helper, program); err != Error::None)
            {
                return makeUnexpected(err);
            }

            return { std::move(program) };
        }
        catch (Error err)
        {
            return makeUnexpected(err);
        }
    }

    zasm::Expected<Program, Error> load(std::filesystem::path inputFilePath)
    {
        FileStream stream(inputFilePath, StreamMode::Read);
        if (!stream.isOpen())
        {
            return makeUnexpected(Error::AccessDenied);
        }

        return load(stream);
    }

} // namespace zasm
