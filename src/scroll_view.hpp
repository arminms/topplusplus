// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#pragma once

#include <utility>
#include <memory>

#include <ftxui/ftxui.hpp>

namespace
{   struct scroll_state
    {   int offset = 0;
        int max_offset = 0;
        int view_height = 1;
        ftxui::Box view;
    };

    // Vertical viewport whose offset is controlled by the application.
    ftxui::Decorator scroll_view(scroll_state& st)
    {   class Impl : public ftxui::Node
        {   public:
                Impl(ftxui::Element child, scroll_state& st)
                :   Node(ftxui::Elements{std::move(child)}), st_(st) {}
                void ComputeRequirement() override
                {   Node::ComputeRequirement();
                    requirement_ = children_[0]->requirement();
                    requirement_.min_y = 1;
                    requirement_.flex_grow_y = 1;
                    requirement_.flex_shrink_y = 1;
                }
                void SetBox(ftxui::Box box) override
                {   Node::SetBox(box);
                    const int height = box.y_max - box.y_min + 1;
                    const int content
                    =   std::max(children_[0]->requirement().min_y, height);
                    st_.view = box;
                    st_.view_height = std::max(1, height);
                    st_.max_offset = content - height;
                    st_.offset = std::clamp(st_.offset, 0, st_.max_offset);
                    ftxui::Box cb = box;
                    cb.y_min = box.y_min - st_.offset;
                    cb.y_max = cb.y_min + content - 1;
                    children_[0]->SetBox(cb);
                }
                void Render(ftxui::Screen& screen) override
                {   const ftxui::Box saved = screen.stencil;
                    screen.stencil = ftxui::Box::Intersection(box_, saved);
                    struct restore { ftxui::Box& r; ftxui::Box v; ~restore() { r = v; } } rs{screen.stencil, saved};
                    children_[0]->Render(screen);
                    if (st_.max_offset <= 0)
                        return;
                    const int height = box_.y_max - box_.y_min + 1;
                    const int content = height + st_.max_offset;
                    const int size = std::max(1, height * height / content);
                    const int start
                    =   st_.offset * (height - size) / st_.max_offset;
                    for (int y = 0; y < height; ++y)
                    {   auto& px = screen.PixelAt(box_.x_max, box_.y_min + y);
                        px.character = y >= start && y < start + size ? "┃" : " ";
                    }
                }
            private:
                scroll_state& st_;
        };
        return [&st](ftxui::Element child)
        {   return std::make_shared<Impl>(std::move(child), st); };
    }
}
