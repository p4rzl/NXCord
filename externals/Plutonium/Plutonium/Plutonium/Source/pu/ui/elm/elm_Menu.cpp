#include <pu/ui/elm/elm_Menu.hpp>
#include <algorithm>

namespace pu::ui::elm
{
    MenuItem::MenuItem(String Name)
    {
        this->clr = { 10, 10, 10, 255 };
        this->name = Name;
        this->hasicon = false;
    }

    String MenuItem::GetName()
    {
        return this->name;
    }

    void MenuItem::SetName(String Name)
    {
        this->name = Name;
    }

    Color MenuItem::GetColor()
    {
        return this->clr;
    }

    void MenuItem::SetColor(Color Color)
    {
        this->clr = Color;
    }

    void MenuItem::AddOnClick(std::function<void()> Callback, u64 Key)
    {
        this->cbs.push_back(Callback);
        this->cbipts.push_back(Key);
    }

    s32 MenuItem::GetCallbackCount()
    {
        return this->cbs.size();
    }

    std::function<void()> MenuItem::GetCallback(s32 Index)
    {
        if(this->cbs.empty()) return [&](){};
        return this->cbs[Index];
    }

    u64 MenuItem::GetCallbackKey(s32 Index)
    {
        return this->cbipts[Index];
    }

    std::string MenuItem::GetIcon()
    {
        return this->icon;
    }

    void MenuItem::SetIcon(std::string Icon)
    {
        std::ifstream ifs(Icon);
        if(ifs.good())
        {
            this->icon = Icon;
            this->hasicon = true;
        }
        ifs.close();
    }

    bool MenuItem::HasIcon()
    {
        return this->hasicon;
    }

    Menu::Menu(s32 X, s32 Y, s32 Width, Color OptionColor, s32 ItemSize, s32 ItemsToShow) : Element::Element()
    {
        this->x = X;
        this->y = Y;
        this->w = Width;
        this->clr = OptionColor;
        this->scb = { 110, 110, 110, 255 };
        this->isize = ItemSize;
        this->ishow = ItemsToShow;
        this->previsel = 0;
        this->isel = 0;
        this->fisel = 0;
        this->selfact = 255;
        this->pselfact = 0;
        this->onselch = [&](){};
        this->icdown = false;
        this->dtouch = false;
        this->fcs = { 40, 40, 40, 255 };
        this->basestatus = 0;
        this->font = render::LoadDefaultFont(25);
    }

    s32 Menu::GetX()
    {
        return this->x;
    }

    Menu::~Menu()
    {
        ClearItems();
    }

    void Menu::SetX(s32 X)
    {
        this->x = X;
    }

    s32 Menu::GetY()
    {
        return this->y;
    }

    void Menu::SetY(s32 Y)
    {
        this->y = Y;
    }

    s32 Menu::GetWidth()
    {
        return this->w;
    }

    void Menu::SetWidth(s32 Width)
    {
        this->w = Width;
    }

    s32 Menu::GetHeight()
    {
        return (this->isize * this->ishow);
    }

    s32 Menu::GetItemSize()
    {
        return this->isize;
    }

    void Menu::SetItemSize(s32 ItemSize)
    {
        this->isize = ItemSize;
    }

    s32 Menu::GetNumberOfItemsToShow()
    {
        return this->ishow;
    }

    void Menu::SetNumberOfItemsToShow(s32 ItemsToShow)
    {
        this->ishow = ItemsToShow;
    }

    Color Menu::GetColor()
    {
        return this->clr;
    }

    void Menu::SetColor(Color Color)
    {
        this->clr = Color;
    }
    
    Color Menu::GetOnFocusColor()
    {
        return this->fcs;
    }

    void Menu::SetOnFocusColor(Color Color)
    {
        this->fcs = Color;
    }

    Color Menu::GetScrollbarColor()
    {
        return this->scb;
    }

    void Menu::SetScrollbarColor(Color Color)
    {
        this->scb = Color;
    }

    void Menu::SetOnSelectionChanged(std::function<void()> Callback)
    {
        this->onselch = Callback;
    }

    void Menu::AddItem(MenuItem::Ref &Item)
    {
        this->itms.push_back(Item);
    }

    void Menu::ClearItems()
    {
        for(auto texture: this->loadednames) render::DeleteTexture(texture);
        for(auto texture: this->loadedicons) render::DeleteTexture(texture);
        this->itms.clear();
        this->loadednames.clear();
        this->loadedicons.clear();
        this->isel = this->fisel = this->previsel = 0;
        this->dtouch = false;
        this->basestatus = 0;
    }

    void Menu::SetCooldownEnabled(bool Cooldown)
    {
        this->icdown = Cooldown;
    }

    MenuItem::Ref &Menu::GetSelectedItem()
    {
        return this->itms[this->isel];
    }

    std::vector<MenuItem::Ref> &Menu::GetItems()
    {
        return this->itms;
    }

    s32 Menu::GetSelectedIndex()
    {
        return this->isel;
    }

    void Menu::SetSelectedIndex(s32 Index)
    {
        if(!this->itms.empty())
        {
            const s32 old_first = this->fisel;
            this->isel = std::clamp(Index, 0, static_cast<s32>(this->itms.size()) - 1);
            this->fisel = std::clamp(this->fisel, 0,
                std::max(0, static_cast<s32>(this->itms.size()) - this->ishow));
            if(this->isel < this->fisel) this->fisel = this->isel;
            if(this->isel >= this->fisel + this->ishow) this->fisel = this->isel - this->ishow + 1;
            if(old_first != this->fisel || this->loadednames.empty()) ReloadItemRenders();
            this->selfact = 255;
            this->pselfact = 0;
        }
    }

    void Menu::OnRender(render::Renderer::Ref &Drawer, s32 X, s32 Y)
    {
        if(!this->itms.empty())
        {
            s32 cx = X;
            s32 cy = Y;
            s32 cw = this->w;
            s32 ch = this->isize;
            s32 its = this->ishow;
            if(its > this->itms.size()) its = this->itms.size();
            if((its + this->fisel) > this->itms.size()) its = this->itms.size() - this->fisel;
            if(this->loadednames.empty()) ReloadItemRenders();
            for(s32 i = this->fisel; i < (its + this->fisel); i++)
            {
                s32 clrr = this->clr.R;
                s32 clrg = this->clr.G;
                s32 clrb = this->clr.B;
                s32 nr = clrr - 70;
                if(nr < 0) nr = 0;
                s32 ng = clrg - 70;
                if(ng < 0) ng = 0;
                s32 nb = clrb - 70;
                if(nb < 0) nb = 0;
                Color nclr(nr, ng, nb, this->clr.A);
                auto loadedidx = i - this->fisel;
                auto curname = this->loadednames[loadedidx];
                auto curicon = this->loadedicons[loadedidx];
                if(this->isel == i)
                {
                    Drawer->RenderRectangleFill(this->clr, cx, cy, cw, ch);
                    if(this->selfact < 255)
                    {
                        Drawer->RenderRectangleFill(Color(this->fcs.R, this->fcs.G, this->fcs.B, this->selfact), cx, cy, cw, ch);
                        this->selfact += 48;
                    }
                    else Drawer->RenderRectangleFill(this->fcs, cx, cy, cw, ch);
                }
                else if(this->previsel == i)
                {
                    Drawer->RenderRectangleFill(this->clr, cx, cy, cw, ch);
                    if(this->pselfact > 0)
                    {
                        Drawer->RenderRectangleFill(Color(this->fcs.R, this->fcs.G, this->fcs.B, this->pselfact), cx, cy, cw, ch);
                        this->pselfact -= 48;
                    }
                    else Drawer->RenderRectangleFill(this->clr, cx, cy, cw, ch);
                }
                else Drawer->RenderRectangleFill(this->clr, cx, cy, cw, ch);
                auto itm = this->itms[i];
                s32 xh = render::GetTextHeight(this->font, itm->GetName());
                s32 tx = (cx + 25);
                s32 ty = ((ch - xh) / 2) + cy;
                if(itm->HasIcon())
                {
                    float factor = (float)render::GetTextureHeight(curicon)/(float)render::GetTextureWidth(curicon);
                    s32 icw = (this->isize - 10);
                    s32 ich = icw;
                    s32 icx = (cx + 25);
                    s32 icy = (cy + 5);
                    tx = (icx + icw + 25);
                    if(factor < 1)
                    {
                        ich = ich*factor;
                        icy = icy+((this->isize-ich)/2);
                    } else
                    {
                        icw = icw/factor;
                        icx = icx+((this->isize-icw)/2);
                    }
                    Drawer->RenderTexture(curicon, icx, icy, { -1, icw, ich, -1.0f });
                }
                Drawer->RenderTexture(curname, tx, ty);
                cy += ch;
            }
            if(this->ishow < this->itms.size())
            {
                s32 sccr = this->scb.R;
                s32 sccg = this->scb.G;
                s32 sccb = this->scb.B;
                s32 snr = sccr - 30;
                if(snr < 0) snr = 0;
                s32 sng = sccg - 30;
                if(sng < 0) sng = 0;
                s32 snb = sccb - 30;
                if(snb < 0) snb = 0;
                Color sclr(snr, sng, snb, this->scb.A);
                s32 scx = X + (this->w - 20);
                s32 scy = Y;
                s32 scw = 20;
                s32 sch = (this->ishow * this->isize);
                Drawer->RenderRectangleFill(this->scb, scx, scy, scw, sch);
                s32 fch = ((this->ishow * sch) / this->itms.size());
                s32 fcy = scy + (this->fisel * (sch / this->itms.size()));
                Drawer->RenderRectangleFill(sclr, scx, fcy, scw, fch);
            }
            Drawer->RenderShadowSimple(cx, cy, cw, 5, 160);
        }
    }

    void Menu::OnInput(u64 Down, u64 Up, u64 Held, Touch Pos)
    {
        // Bound indices after a live refresh, and keep callbacks alive even
        // when they navigate away from (or rebuild) this menu.
        if(itms.empty()) return;
        if(isel < 0 || isel >= static_cast<s32>(itms.size())) SetSelectedIndex(0);
        if(!Pos.IsEmpty())
        {
            const s32 local_y = Pos.Y - GetProcessedY();
            if(Pos.X >= GetProcessedX() && Pos.X < GetProcessedX() + w &&
               local_y >= 0 && local_y < isize * ishow)
            {
                const s32 index = fisel + local_y / isize;
                if(index < static_cast<s32>(itms.size()))
                {
                    SetSelectedIndex(index);
                    dtouch = true;
                    onselch();
                }
            }
            return;
        }
        if(dtouch)
        {
            dtouch = false;
            auto item = itms[isel];
            if(item->GetCallbackCount()) { auto callback = item->GetCallback(0); callback(); }
            return;
        }
        const u64 down_keys = KEY_DDOWN | KEY_LSTICK_DOWN | KEY_RSTICK_DOWN;
        const u64 up_keys = KEY_DUP | KEY_LSTICK_UP | KEY_RSTICK_UP;
        const auto now = std::chrono::steady_clock::now();
        int direction = Down & down_keys ? 1 : Down & up_keys ? -1 : 0;
        if(direction) { basetime = now; basestatus = 1; }
        else if(Held & (down_keys | up_keys))
        {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - basetime).count();
            if(basestatus && elapsed >= (basestatus == 1 ? 320 : 90))
            {
                direction = Held & down_keys ? 1 : -1;
                basetime = now;
                basestatus = 2;
            }
        }
        else basestatus = 0;
        if(direction || (Down & (KEY_L | KEY_R)))
        {
            const int step = Down & KEY_R ? ishow : Down & KEY_L ? -ishow : direction;
            SetSelectedIndex(isel + step);
            onselch();
            return;
        }
        auto item = itms[isel];
        for(s32 i = 0; i < item->GetCallbackCount(); ++i)
        {
            if(Down & item->GetCallbackKey(i))
            {
                auto callback = item->GetCallback(i);
                callback();
                return;
            }
        }
    }

    void Menu::ReloadItemRenders()
    {
        for(u32 i = 0; i < this->loadednames.size(); i++) render::DeleteTexture(this->loadednames[i]);
        for(u32 i = 0; i < this->loadedicons.size(); i++) render::DeleteTexture(this->loadedicons[i]);
        this->loadednames.clear();
        this->loadedicons.clear();
        s32 its = this->ishow;
        if(its > this->itms.size()) its = this->itms.size();
        if((its + this->fisel) > this->itms.size()) its = this->itms.size() - this->fisel;
        for(s32 i = this->fisel; i < (its + this->fisel); i++)
        {
            auto strname = this->itms[i]->GetName();
            auto tex = render::RenderText(this->font, strname, this->itms[i]->GetColor());
            this->loadednames.push_back(tex);
            if(this->itms[i]->HasIcon())
            {
                auto stricon = this->itms[i]->GetIcon();
                auto icontex = render::LoadImage(stricon);
                this->loadedicons.push_back(icontex);
            }
            else this->loadedicons.push_back(NULL);
        }
    }
}
