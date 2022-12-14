//****************************************************************************
// Copyright © 2022 Jan Erik Breimo. All rights reserved.
// Created by Jan Erik Breimo on 2022-09-14.
//
// This file is distributed under the BSD License.
// License text is included with the source distribution.
//****************************************************************************
#include <iostream>
#include <Argos/Argos.hpp>
#include <Tungsten/Tungsten.hpp>
#include <Yimage/Yimage.hpp>
#include "ObjFileWriter.hpp"
#include "Render3DShaderProgram.hpp"

struct Vertex
{
    Xyz::Vector3F pos;
    Xyz::Vector2F tex;
};

Tungsten::ArrayBuffer<Vertex> make_sphere(int circles, int points)
{
    if (circles < 2)
        throw std::runtime_error("Number of circles must be at least 2.");
    if (points < 3)
        throw std::runtime_error("Number of points must be at least 3.");
    Tungsten::ArrayBuffer<Vertex> result;
    Tungsten::ArrayBufferBuilder builder(result);

    constexpr auto PI = Xyz::Constants<float>::PI;

    std::vector<float> pos_z_values;
    std::vector<float> z_factors;
    std::vector<float> tex_y_values;
    for (int i = 0; i < circles; ++i)
    {
        const float angle = 0.5f * (-1.f + float(2 * i + 1) / float(circles)) * PI;
        pos_z_values.push_back(sin(angle));
        z_factors.push_back(cos(angle));
        //tex_y_values.push_back(1.f - 0.5f * (1 + sin(angle)));
        tex_y_values.push_back(1.f - (float(i) + 0.5f) / float(circles));
    }

    for (int i = 0; i <= points; ++i)
    {
        const float angle = (float(i) * 2.f / float(points) - 0.5f) * PI;
        const float pos_x =  cos(angle);
        const float pos_y =  sin(angle);
        const float tex_x = float(i) / float(points);
        for (int j = 0; j < circles; ++j)
        {
            builder.add_vertex({.pos = {pos_x * z_factors[j],
                                        pos_y * z_factors[j],
                                        pos_z_values[j]},
                                   .tex = {tex_x, tex_y_values[j]}});
        }
    }

    for (int i = 0; i < points; ++i)
    {
        for (int j = 0; j < circles - 1; ++j)
        {
            const auto n = uint16_t(i * circles + j);
            builder.add_indexes(n, n + 1, n + circles + 1);
            builder.add_indexes(n, n + circles + 1, n + circles);
        }
    }

    return result;
}

void write(std::ostream& os, Tungsten::ArrayBuffer<Vertex>& buffer)
{
    ObjFileWriter writer(os);
    for (const auto& vertex : buffer.vertexes)
        writer.write_vertex(vertex.pos);

    for (const auto& vertex : buffer.vertexes)
        writer.write_tex(vertex.tex);

    for (size_t i = 0; i < buffer.indexes.size(); i += 3)
    {
        writer.begin_face();
        for (size_t j = 0; j < 3; ++j)
        {
            auto n = 1 + buffer.indexes[i + j];
            writer.write_face({n, n});
        }
        writer.end_face();
    }
}

std::pair<int, int> get_ogl_pixel_type(yimage::PixelType type)
{
    switch (type)
    {
    case yimage::PixelType::MONO_8:
        return {GL_RED, GL_UNSIGNED_BYTE};
    case yimage::PixelType::MONO_ALPHA_8:
        return {GL_RG, GL_UNSIGNED_BYTE};
    case yimage::PixelType::RGB_8:
        return {GL_RGB, GL_UNSIGNED_BYTE};
    case yimage::PixelType::RGBA_8:
        return {GL_RGBA, GL_UNSIGNED_BYTE};
    case yimage::PixelType::MONO_1:
    case yimage::PixelType::MONO_2:
    case yimage::PixelType::MONO_4:
    case yimage::PixelType::MONO_16:
    case yimage::PixelType::ALPHA_MONO_8:
    case yimage::PixelType::ALPHA_MONO_16:
    case yimage::PixelType::MONO_ALPHA_16:
    case yimage::PixelType::RGB_16:
    case yimage::PixelType::ARGB_8:
    case yimage::PixelType::ARGB_16:
    case yimage::PixelType::RGBA_16:
    default:
        break;
    }
    throw std::runtime_error("GLES has no corresponding pixel format: "
                             + std::to_string(int(type)));
}

class ImageViewer : public Tungsten::EventLoop
{
public:
    explicit ImageViewer(yimage::Image img)
        : img_(std::move(img))
    {}

    void on_startup(Tungsten::SdlApplication& app) override
    {
        auto array = make_sphere(3, 128);
        write(std::cout, array);
        vertex_array_ = Tungsten::generate_vertex_array();
        Tungsten::bind_vertex_array(vertex_array_);

        auto [vertexes, vertexes_size] = array.array_buffer();
        auto [indexes, index_size] = array.index_buffer();
        count_ = index_size;
        buffers_ = Tungsten::generate_buffers(2);
        Tungsten::bind_buffer(GL_ARRAY_BUFFER, buffers_[0]);
        Tungsten::set_buffer_data(GL_ARRAY_BUFFER, vertexes_size,
                                  vertexes, GL_STATIC_DRAW);
        Tungsten::bind_buffer(GL_ELEMENT_ARRAY_BUFFER, buffers_[1]);
        Tungsten::set_buffer_data(GL_ELEMENT_ARRAY_BUFFER, index_size,
                                  indexes, GL_STATIC_DRAW);

        texture_ = Tungsten::generate_texture();
        Tungsten::bind_texture(GL_TEXTURE_2D, texture_);

        auto [format, type] = get_ogl_pixel_type(img_.pixel_type());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        Tungsten::set_texture_image_2d(GL_TEXTURE_2D, 0, GL_RGBA,
                                       int(img_.width()), int(img_.height()),
                                       format, type,
                                       img_.data());
        img_ = {};

        Tungsten::set_texture_min_filter(GL_TEXTURE_2D, GL_LINEAR);
        Tungsten::set_texture_mag_filter(GL_TEXTURE_2D, GL_LINEAR);
        Tungsten::set_texture_parameter(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        Tungsten::set_texture_parameter(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        program_.setup();
        Tungsten::define_vertex_attribute_pointer(
            program_.position, 3, GL_FLOAT, false, 5 * sizeof(float), 0);
        Tungsten::enable_vertex_attribute(program_.position);
        Tungsten::define_vertex_attribute_pointer(
            program_.texture_coord, 2, GL_FLOAT, true, 5 * sizeof(float),
            3 * sizeof(float));
        Tungsten::enable_vertex_attribute(program_.texture_coord);
    }

    bool on_event(Tungsten::SdlApplication& app, const SDL_Event& event) override
    {
        return EventLoop::on_event(app, event);
    }

    void on_draw(Tungsten::SdlApplication& app) override
    {
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        program_.mv_matrix.set(get_mv_matrix(app));
        program_.p_matrix.set(get_p_matrix(app));
        Tungsten::draw_elements(GL_TRIANGLES, count_, GL_UNSIGNED_SHORT);
    }
private:
    Xyz::Matrix4F get_mv_matrix(const Tungsten::SdlApplication& app)
    {
        return Xyz::make_look_at_matrix<float>({0.f, 0.f, 0.f},
                                               {0.f, -1.f, 0.f},
                                               {0.f, 0.f, 1.f})
               * Xyz::scale4(4.f, 4.f, 4.f);
    }

    Xyz::Matrix4F get_p_matrix(const Tungsten::SdlApplication& app)
    {
        return Xyz::make_frustum_matrix<float>(-2, 2, -2, 2, 1, 10);
    }

    unsigned count_ = 0;
    yimage::Image img_;
    std::vector<Tungsten::BufferHandle> buffers_;
    Tungsten::VertexArrayHandle vertex_array_;
    Tungsten::TextureHandle texture_;
    Render3DShaderProgram program_;
};

int main(int argc, char* argv[])
{
    try
    {
        argos::ArgumentParser parser(argv[0]);
        parser.add(argos::Argument("IMAGE")
                       .help("An image file (PNG or JPEG)."));
        Tungsten::SdlApplication::add_command_line_options(parser);
        auto args = parser.parse(argc, argv);
        auto event_loop = std::make_unique<ImageViewer>(
            yimage::read_image(args.value("IMAGE").as_string()));
        Tungsten::SdlApplication app("ShowPng", std::move(event_loop));
        app.read_command_line_options(args);
        app.run();
    }
    catch (std::exception& ex)
    {
        std::cout << ex.what() << "\n";
        return 1;
    }
    return 0;
}
