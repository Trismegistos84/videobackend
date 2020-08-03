/*
This is fragment of C++ code of an audio mixer with arbitrary number of inputs (channels) and outputs (buses). Each (Input/Output)Port is single-channel.

1. Complete the block marked with TODO (MatrixPoint::process) to make MatrixPoint object work as a single "knob" or "fader" controlling single input -> output send's gain.

2. Dsp::refAll and Dsp::unrefAll are called before Dsp object is used for the first time, and when it is no longer needed, respectively. Why do Port and MatrixPoint inherit from CountUsers? Is there any similar mechanism in standard library?

3. Dsp::process is called from a real-time thread. If it takes too long, sound dropouts will occur. What system calls need to be avoided to prevent it from happening?
*/

#include <iostream>
#include <string>
#include <vector>
#include <atomic>
#include <jack/jack.h>

jack_client_t* jack_client = nullptr;

class CountUsers {
protected:
    unsigned int users_count_ = 0;
public:
    void incrementUsers() {
        users_count_++;
        std::cerr << "++ @" << this << std::endl;
    }
    void decrementUsers() {
        assert(users_count_);
        users_count_--;
        std::cerr << "-- @" << this << std::endl;
    }
    bool hasUsers() {
        return users_count_;
    }
};

class Port: public CountUsers {
protected:
    jack_port_t* jack_port_;
    std::string name_;
    float* buffer_;
public:
    Port(const std::string name, unsigned long flags): name_(name) {
        jack_port_ = jack_port_register(jack_client, name.c_str(), JACK_DEFAULT_AUDIO_TYPE, flags, 0);
    }
    Port(const Port&) = delete;
    Port(Port&&) = delete;
    ~Port() {
        jack_port_unregister(jack_client, jack_port_);
    }
    void updateBufferAddress(jack_nframes_t nframes) {
        buffer_ = reinterpret_cast<float*>(jack_port_get_buffer(jack_port_, nframes));
    }
    float* buffer() {
        assert(buffer_);
        return buffer_;
    }
    std::string name() const {
        return name_;
    }
};

class InputPort: public Port {
public:
    InputPort(const std::string name): Port(name, JackPortIsInput) {
    }
};

class OutputPort: public Port {
public:
    OutputPort(const std::string name): Port(name, JackPortIsOutput) {
    }
    void resetBuffer(const jack_nframes_t nframes) {
        std::fill(buffer_, buffer_+nframes, 0);
    }
};

class MatrixPoint: public CountUsers {
protected:
    InputPort *input_;
    OutputPort *output_;
    std::atomic<float> gain_;
public:
    MatrixPoint(InputPort *input, OutputPort *output, float gain = 0): input_(input), output_(output), gain_(gain) {
        input_->incrementUsers();
        output_->incrementUsers();
    }
    MatrixPoint(const MatrixPoint& copyfrom): input_(copyfrom.input_), output_(copyfrom.output_), gain_(copyfrom.gain_.load()) {
        input_->incrementUsers();
        output_->incrementUsers();
    }
    MatrixPoint(MatrixPoint &&movefrom): input_(movefrom.input_), output_(movefrom.output_), gain_(movefrom.gain_.load()) {
        movefrom.input_ = nullptr;
        movefrom.output_ = nullptr;
    }
    ~MatrixPoint() {
        if (input_) input_->decrementUsers();
        if (output_) output_->decrementUsers();
    }
    void process(jack_nframes_t samples_count) {
        // assume buffer does not change throughout frame processing.
        // it is also optimization and code is preattier.        
        float * const in_buffer = input_->buffer();
        float * const out_buffer = output_->buffer();

        // I assume here that gain is calculated to exact number e.g.
        // if someone sets gain in decibels this class receives number 
        // calculated to right scale e.g. gain_ = 10**gain_in_db/20;
        // I also cache gain so not to bear high cost of multithreaded synchronization
        // everytime loop iteration and to have consistent gain throughout whole frame. 
        // This also exhibits better realtime behaviour - no synchronization, gain probably
        // stored in local core cache or register and not shared with other threads.
        // Having consistent gain throughout whole frame may be not what is required 
        // but that needs specification.
        const float gain = gain_.load();

        for (jack_nframes_t i = 0; i < samples_count; ++i) {
            // += because many inputs can be directed to
            // single output so we want to sum them up.
            // Buffer is zeroed before process is called.
            // again requirements did not specified if this is
            // the case so = may be sufficient here.
            out_buffer[i] += gain * in_buffer[i];
        }
    }
    void setGain(const float gain) { gain_.store(gain); }
    InputPort* input() { return input_; }
    OutputPort* output() { return output_; }
};

struct Dsp {
    std::vector<InputPort*> inputs;
    std::vector<OutputPort*> outputs;
    std::vector<MatrixPoint*> matrix_points;
    void process(jack_nframes_t nframes) {
        for (InputPort* port: inputs) {
            port->updateBufferAddress(nframes);
        }
        for (OutputPort* port: outputs) {
            port->updateBufferAddress(nframes);
            port->resetBuffer(nframes);
        }
        for (MatrixPoint* point: matrix_points) {
            point->process(nframes);
        }
    }
    void refAll() {
        #define ref(container) { \
            for (auto &elem: container) { elem->incrementUsers(); } \
        }
        ref(inputs);
        ref(outputs);
        ref(matrix_points);
        #undef ref
    }
    void unrefAll() {
        #define clr(container) { \
            for (auto &elem: container) { elem->decrementUsers(); } \
            container.clear(); \
        }
        clr(inputs);
        clr(outputs);
        clr(matrix_points);
        #undef clr
    }
};
