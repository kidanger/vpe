#pragma once

#include <vector>

#include <process.hpp>
using namespace TinyProcessLib;

class Block
{

public:

    virtual ~Block()
    {
    }

    virtual void Launch() = 0;
    virtual void Stop() = 0;
    virtual bool IsRunning() = 0;
};

class CommandBlock : public Block
{
    std::string command;
    TinyProcessLib::Process* process = nullptr;
    std::string consoleOutput;

public:

    CommandBlock(const std::string command) : command(command) {}

    virtual ~CommandBlock()
    {
        delete process;
    }

    virtual void Launch() override
    {
        if (IsRunning())
        {
            Stop();
        }
        consoleOutput.clear();
        auto clb = [this](const char *bytes, size_t n) {
            consoleOutput += std::string(bytes, n);
        };
        process = new Process(command, "", clb);
        printf("%s\n", command.c_str());
    }

    virtual void Stop() override
    {
        if (process)
        {
            process->kill(true);
        }
    }

    virtual bool IsRunning() override
    {
        int status;
        return process && !process->try_get_exit_status(status);
    }

    const std::string& GetOutput() const
    {
        return consoleOutput;
    }
};

class Pipeline
{
std::vector<Block*> blocks;

public:

    void Launch()
    {
        for (auto b : blocks)
            b->Launch();
    }

    void Stop()
    {
        for (auto b : blocks)
            b->Stop();
    }

    bool IsRunning()
    {
        for (auto b : blocks)
        {
            if (b->IsRunning())
                return true;
        }
        return false;
    }

    void Clear()
    {
        //for (auto b : blocks)
            //delete b;
        blocks.clear();
    }

    void Add(Block* b)
    {
        blocks.push_back(b);
    }

};

