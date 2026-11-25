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
    ftxui::Decorator scroll_view
    (   scroll_state& st, bool reserve_scrollbar = false, int sticky_rows = 0
    )
    {   class Impl : public ftxui::Node
        {   public:
                Impl(ftxui::Element child, scroll_state& st, bool reserve_scrollbar, int sticky_rows)
                :   Node(ftxui::Elements{std::move(child)})
                ,   st_(st)
                ,   reserve_scrollbar_(reserve_scrollbar)
                ,   sticky_rows_(sticky_rows)
                {}
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
                    pinned_ = std::clamp(sticky_rows_, 0, std::max(0, height - 1));
                    st_.view = box;
                    st_.view_height = std::max(1, height - pinned_);
                    st_.max_offset = content - height;
                    st_.offset = std::clamp(st_.offset, 0, st_.max_offset);
                    child_box_ = box;
                    child_box_.y_max = box.y_min + content - 1;
                    if (reserve_scrollbar_ && st_.max_offset > 0 && child_box_.x_max > child_box_.x_min)
                        --child_box_.x_max;
                    auto scrolled_box = child_box_;
                    scrolled_box.y_min -= st_.offset;
                    scrolled_box.y_max -= st_.offset;
                    children_[0]->SetBox(scrolled_box);
                }
                void Render(ftxui::Screen& screen) override
                {   const ftxui::Box saved = screen.stencil;
                    screen.stencil = ftxui::Box::Intersection(box_, saved);
                    struct restore { ftxui::Box& r; ftxui::Box v; ~restore() { r = v; } } rs{screen.stencil, saved};
                    if (pinned_ > 0 && st_.offset > 0)
                    {   // Render both regions from the same layout to keep columns aligned.
                        auto header_box = box_;
                        header_box.y_max = header_box.y_min + pinned_ - 1;
                        screen.stencil = ftxui::Box::Intersection(header_box, saved);
                        children_[0]->SetBox(child_box_);
                        children_[0]->Render(screen);
                        auto scrolled_box = child_box_;
                        scrolled_box.y_min -= st_.offset;
                        scrolled_box.y_max -= st_.offset;
                        children_[0]->SetBox(scrolled_box);
                        auto body_box = box_;
                        body_box.y_min += pinned_;
                        screen.stencil = ftxui::Box::Intersection(body_box, saved);
                    }
                    children_[0]->Render(screen);
                    if (st_.max_offset <= 0)
                        return;
                    const int height = box_.y_max - box_.y_min + 1 - pinned_;
                    const int content = height + st_.max_offset;
                    const int size = std::max(1, height * height / content);
                    const int start
                    =   st_.offset * (height - size) / st_.max_offset;
                    for (int y = 0; y < height; ++y)
                    {   auto& px = screen.PixelAt(box_.x_max, box_.y_min + pinned_ + y);
                        px.character = y >= start && y < start + size ? "┃" : " ";
                    }
                }
            private:
                scroll_state& st_;
                bool reserve_scrollbar_;
                int sticky_rows_;
                int pinned_ = 0;
                ftxui::Box child_box_;
        };
        return [&st, reserve_scrollbar, sticky_rows](ftxui::Element child)
        {   return std::make_shared<Impl>(std::move(child), st, reserve_scrollbar, sticky_rows); };
    }
}
