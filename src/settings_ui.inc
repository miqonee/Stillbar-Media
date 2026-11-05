    islands::modern::Palette palette=islands::modern::Palette::system();
    HWND body=nullptr,apps_auto=nullptr,about=nullptr,about_hint=nullptr,folder=nullptr,advanced_hint=nullptr,mode_link=nullptr;
    struct Placement{HWND window;int x,y,width,height;};
    std::vector<Placement> placements;
    std::array<HWND,5> navigation{};
    HWND browser_label=nullptr,browser_hint=nullptr,browser_connect=nullptr,browser_check=nullptr,browser_disable=nullptr,browser_developer=nullptr;
    std::wstring browser_error;
    ULONGLONG browser_open_until=0;
    HFONT symbols=nullptr;
    HBRUSH background_brush=nullptr,card_brush=nullptr;
    int page=0,scroll=0,body_height=0,body_width=640,extent=0;
    bool arranging=false;
    std::vector<RECT> cards;

    std::wstring version_text()const{const std::string value=STILLBAR_VERSION_STRING;return {value.begin(),value.end()};}
    void theme(){
        palette=islands::modern::Palette::system();
        if(background_brush)DeleteObject(background_brush);if(card_brush)DeleteObject(card_brush);
        background_brush=CreateSolidBrush(palette.background);card_brush=CreateSolidBrush(palette.card);
        const BOOL dark=palette.dark;DwmSetWindowAttribute(window,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
        const DWM_SYSTEMBACKDROP_TYPE backdrop=DWMSBT_MAINWINDOW;DwmSetWindowAttribute(window,DWMWA_SYSTEMBACKDROP_TYPE,&backdrop,sizeof(backdrop));
        if(body)SetWindowTheme(body,palette.dark?L"DarkMode_Explorer":L"Explorer",nullptr);
        if(body)for(auto child=GetWindow(body,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))SetWindowTheme(child,palette.dark?L"DarkMode_Explorer":L"Explorer",nullptr);
        InvalidateRect(window,nullptr,TRUE);if(body)InvalidateRect(body,nullptr,TRUE);
    }
    void update_fonts(){
        const auto old_font=font,old_title=title_font,old_symbols=symbols;
        font=CreateFontW(-px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        title_font=CreateFontW(-MulDiv(18,dpi,72),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        symbols=CreateFontW(-px(17),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe Fluent Icons");
        EnumChildWindows(window,[](HWND child,LPARAM value)->BOOL{SendMessageW(child,WM_SETFONT,static_cast<WPARAM>(value),TRUE);return TRUE;},reinterpret_cast<LPARAM>(font));
        if(title_label)SendMessageW(title_label,WM_SETFONT,reinterpret_cast<WPARAM>(title_font),TRUE);if(about)SendMessageW(about,WM_SETFONT,reinterpret_cast<WPARAM>(title_font),TRUE);
        if(old_font)DeleteObject(old_font);if(old_title)DeleteObject(old_title);if(old_symbols)DeleteObject(old_symbols);
    }
    void create_controls(){
        const auto instance=GetModuleHandleW(nullptr);dpi=GetDpiForWindow(window);
        update_fonts();
        icon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_STILLBAR),IMAGE_ICON,GetSystemMetricsForDpi(SM_CXICON,dpi),GetSystemMetricsForDpi(SM_CYICON,dpi),0));
        small_icon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_STILLBAR),IMAGE_ICON,GetSystemMetricsForDpi(SM_CXSMICON,dpi),GetSystemMetricsForDpi(SM_CYSMICON,dpi),0));
        SendMessageW(window,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(icon));SendMessageW(window,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(small_icon));
        title_label=control(L"STATIC",L"Панель задач",SS_LEFT,50,false);SendMessageW(title_label,WM_SETFONT,reinterpret_cast<WPARAM>(title_font),TRUE);
        subtitle=control(L"STATIC",L"",SS_LEFT,51,false);status=control(L"STATIC",L"",SS_LEFT,20,false);
        close_hint=control(L"STATIC",L"Изменения сохраняются автоматически",SS_LEFT,41,false);
        enable=control(L"BUTTON",L"Включить",BS_OWNERDRAW|WS_TABSTOP,enable_id,false);disable=control(L"BUTTON",L"Выключить",BS_OWNERDRAW|WS_TABSTOP,disable_id,false);
        const wchar_t* names[]{L"Панель задач",L"Островки",L"Карточка плеера",L"Программа",L"Браузеры"};for(int index=0;index<5;++index)navigation[index]=control(L"BUTTON",names[index],BS_OWNERDRAW|WS_TABSTOP,60+index,false);
        control(L"BUTTON",L"Завершить работу",BS_OWNERDRAW|WS_TABSTOP,exit_id,false);
        WNDCLASSW type{};type.hInstance=instance;type.lpfnWndProc=page_proc;type.lpszClassName=L"Stillbar.SettingsPage";type.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&type);
        body=CreateWindowExW(WS_EX_CONTROLPARENT,type.lpszClassName,L"Параметры",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_VSCROLL,0,0,0,0,window,nullptr,instance,this);
        mode_label=control(L"STATIC",L"Режим панели",SS_LEFT,40);mode=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,mode_id);
        SendMessageW(mode,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Только плеер — штатная панель"));SendMessageW(mode,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Три плавающих островка"));SendMessageW(mode,CB_SETCURSEL,settings.mode==islands::AppearanceMode::islands?1:0,0);
        compact=control(L"BUTTON",L"Тонкий фон штатной панели",BS_AUTOCHECKBOX|WS_TABSTOP,compact_id);outline=control(L"BUTTON",L"Рамка плеера на штатной панели",BS_AUTOCHECKBOX|WS_TABSTOP,outline_id);
        advanced=control(L"BUTTON",L"Отдельное оформление островков",BS_AUTOCHECKBOX|WS_TABSTOP,advanced_id);
        advanced_hint=control(L"STATIC",L"",SS_LEFT,74);mode_link=control(L"BUTTON",L"Перейти к выбору режима",BS_OWNERDRAW|WS_TABSTOP,73);
        apps_auto=control(L"BUTTON",L"Автоматическая ширина по значкам приложений",BS_AUTOCHECKBOX|WS_TABSTOP,58);
        page_hint=control(L"STATIC",L"",SS_LEFT,52);popup_hint=control(L"STATIC",L"Для предпросмотра запустите музыку. Карточка остаётся открытой при настройке.",SS_LEFT,53);
        slider(0,L"Скругление панели",0,24,static_cast<int>(settings.radius));slider(1,L"Отступ от края экрана",0,16,static_cast<int>(settings.inset));slider(2,L"Непрозрачность заливки",0,100,static_cast<int>(settings.opacity*100));slider(3,L"Плавность движения",0,400,settings.animation_ms);
        for(int index=0;index<3;++index){const wchar_t* labels[]{L"Плеер",L"Приложения",L"Системный трей"};island_labels[index]=control(L"STATIC",labels[index],SS_LEFT,30+index);slider(4+index,L"Радиус",0,24,static_cast<int>(settings.radii[index]));slider(7+index,L"Заливка",0,100,static_cast<int>(settings.opacities[index]*100));}
        slider(10,L"Сила размытия",0,48,settings.popup_blur);slider(11,L"Скругление карточки",0,24,static_cast<int>(settings.popup_radius));slider(12,L"Непрозрачность заливки",0,100,static_cast<int>(settings.popup_opacity*100));
        slider(13,L"Сила размытия островков",0,48,settings.island_blur);slider(14,L"Отступ карточки от панели задач",0,48,static_cast<int>(settings.popup_gap));slider(15,L"Фиксированная ширина блока приложений",200,1200,static_cast<int>(settings.apps_width));
        preview=control(L"BUTTON",L"Показать карточку",BS_OWNERDRAW|WS_TABSTOP,preview_id);popup_outline=control(L"BUTTON",L"Тонкая рамка вокруг карточки",BS_AUTOCHECKBOX|WS_TABSTOP,popup_outline_id);
        visualizer=control(L"BUTTON",L"Фоновые волны под музыку",BS_AUTOCHECKBOX|WS_TABSTOP,visualizer_id);visualizer_hint=control(L"STATIC",L"Цвет берётся из обложки. Волны усиливаются под звук Windows и не занимают место.",SS_LEFT,56);
        autostart=control(L"BUTTON",L"Запускать при входе в Windows",BS_AUTOCHECKBOX|WS_TABSTOP,autostart_id);
        const auto version=L"Stillbar "+version_text();about=control(L"STATIC",version.c_str(),SS_LEFT,70);SendMessageW(about,WM_SETFONT,reinterpret_cast<WPARAM>(title_font),TRUE);
        const auto info=environment.describe()+L"\r\nИзменения снимаются при завершении программы.";about_hint=control(L"STATIC",info.c_str(),SS_LEFT,71);folder=control(L"BUTTON",L"Открыть папку программы",BS_OWNERDRAW|WS_TABSTOP,72);
        browser_label=control(L"STATIC",L"",SS_LEFT,78);browser_hint=control(L"STATIC",L"",SS_LEFT,79);
        browser_connect=control(L"BUTTON",L"Подключить Яндекс Браузер",BS_OWNERDRAW|WS_TABSTOP,75);browser_check=control(L"BUTTON",L"Проверить связь",BS_OWNERDRAW|WS_TABSTOP,76);browser_disable=control(L"BUTTON",L"Отключить",BS_OWNERDRAW|WS_TABSTOP,77);
        browser_developer=control(L"BUTTON",L"Тестовая установка для разработчика",BS_OWNERDRAW|WS_TABSTOP,80);
        SendMessageW(compact,BM_SETCHECK,settings.compact_background?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(outline,BM_SETCHECK,settings.player_outline?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(advanced,BM_SETCHECK,settings.advanced?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(apps_auto,BM_SETCHECK,settings.apps_autosize?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(popup_outline,BM_SETCHECK,settings.popup_outline?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(visualizer,BM_SETCHECK,settings.popup_visualizer?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(autostart,BM_SETCHECK,islands::autostart_enabled()?BST_CHECKED:BST_UNCHECKED,0);
        for(auto child=GetWindow(body,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))SetWindowSubclass(child,islands::modern::control_paint,1,reinterpret_cast<DWORD_PTR>(&palette));
        for(const auto item:navigation)SetWindowSubclass(item,islands::modern::control_paint,1,reinterpret_cast<DWORD_PTR>(&palette));
        for(const auto item:{enable,disable,GetDlgItem(window,exit_id)})SetWindowSubclass(item,islands::modern::control_paint,1,reinterpret_cast<DWORD_PTR>(&palette));
        theme();update_values();layout(true);SendMessageW(window,WM_CHANGEUISTATE,MAKEWPARAM(UIS_SET,UISF_HIDEFOCUS),0);update_status();
    }
    void show(HWND handle,int x,int y,int width,int height){placements.push_back({handle,x,y,width,height});}
    void card(int top,int height){cards.push_back({8,top,body_width-14,top+height});}
    void row(int index,int top){const int width=body_width-58;show(sliders[index].label,24,top,width-88,22);show(sliders[index].value,body_width-110,top,72,22);show(sliders[index].track,18,top+23,width+10,30);}
    void place_content(bool order=false){
        auto batch=BeginDeferWindowPos(static_cast<int>(placements.size()));
        HWND previous=HWND_TOP;
        for(const auto& item:placements){
            if(!batch)break;
            batch=DeferWindowPos(batch,item.window,order?previous:nullptr,px(item.x),px(item.y-scroll),px(item.width),px(item.height),(order?0:SWP_NOZORDER)|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_SHOWWINDOW);previous=item.window;
        }
        if(batch)EndDeferWindowPos(batch);else for(const auto& item:placements){move(item.window,item.x,item.y,item.width,item.height);ShowWindow(item.window,SW_SHOW);}
        RedrawWindow(body,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
    }
    void layout(bool resize=false){
        if(arranging)return;arranging=true;
        if(resize){const auto path=islands::config_path();const int saved_width=std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Window",L"Width",900,path.c_str())),720,1600),saved_height=std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Window",L"Height",710,path.c_str())),520,1200);RECT desired_bounds{0,0,px(saved_width),px(saved_height)};AdjustWindowRectExForDpi(&desired_bounds,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,0,dpi);RECT current{};GetWindowRect(window,&current);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);const int width=std::min(desired_bounds.right-desired_bounds.left,monitor.rcWork.right-monitor.rcWork.left),height=std::min(desired_bounds.bottom-desired_bounds.top,monitor.rcWork.bottom-monitor.rcWork.top);const int left=std::clamp(current.left,monitor.rcWork.left,monitor.rcWork.right-width),top=std::clamp(current.top,monitor.rcWork.top,monitor.rcWork.bottom-height);SetWindowPos(window,nullptr,left,top,width,height,SWP_NOZORDER|SWP_NOACTIVATE);}
        RECT client{};GetClientRect(window,&client);const int width=MulDiv(client.right,96,dpi),height=MulDiv(client.bottom,96,dpi),side=width<820?184:216;body_height=std::max(160,height-174);const int pane_width=std::min(960,std::max(320,width-side-24));
        move(title_label,side+28,20,pane_width-168,38);move(subtitle,side+28,62,pane_width-52,26);move(enable,side+pane_width-124,24,122,34);move(disable,side+pane_width-124,24,122,34);
        move(body,side+12,100,pane_width,body_height);RECT content{};GetClientRect(body,&content);body_width=MulDiv(content.right,96,dpi);
        move(status,side+28,height-65,pane_width-52,40);move(close_hint,side+28,height-25,pane_width-52,20);
        for(int index=0;index<5;++index)move(navigation[index],12,106+index*48,side-24,42);move(GetDlgItem(window,exit_id),16,height-54,side-32,34);
        placements.clear();cards.clear();
        const wchar_t* titles[]{L"Панель задач",L"Островки",L"Карточка плеера",L"Программа",L"Браузеры"};const wchar_t* descriptions[]{L"Режим, размеры и движение панели",L"Общее или отдельное оформление островков",L"Внешний вид и живой предпросмотр",L"Запуск, версия и расположение программы",L"Дополнительная интеграция медиа"};SetWindowTextW(title_label,titles[page]);SetWindowTextW(subtitle,descriptions[page]);
        const bool islands=settings.mode==islands::AppearanceMode::islands;
        if(page==0){
            card(0,110);show(mode_label,24,16,260,22);show(mode,24,46,body_width-62,170);show(page_hint,24,80,body_width-62,22);SetWindowTextW(page_hint,islands?L"Плеер, приложения и трей размещаются отдельно.":L"Материал штатной панели остаётся под управлением Windows.");
            const bool common_radius=!islands || !settings.advanced;
            const int size_height=common_radius?174:122;card(124,size_height);int y=138;if(common_radius){row(0,y);y+=52;}row(1,y);row(3,y+52);
            const int top=124+size_height+14;
            if(islands){
                card(top,settings.apps_autosize?102:164);show(apps_auto,24,top+16,body_width-62,28);show(advanced_hint,24,top+54,body_width-62,38);
                SetWindowTextW(advanced_hint,settings.apps_autosize?L"Ширина подстраивается под количество значков приложений.":L"Ширина задана вручную. При нехватке места Windows покажет переполнение.");
                if(!settings.apps_autosize)row(15,top+104);extent=top+(settings.apps_autosize?118:180);
            }else{card(top,124);show(compact,24,top+18,body_width-62,28);show(outline,24,top+64,body_width-62,28);extent=top+140;}
        }else if(page==1){
            if(!islands){
                card(0,170);show(mode_label,24,18,body_width-62,24);SetWindowTextW(mode_label,L"Сейчас включён режим «Только плеер»");
                show(page_hint,24,54,body_width-62,46);SetWindowTextW(page_hint,L"Для оформления островков выберите режим «Три плавающих островка» на странице панели задач.");show(mode_link,24,114,260,34);extent=186;
            }else{
                const int common_height=settings.advanced?102:154;card(0,common_height);
                if(!settings.advanced)row(2,14);row(13,settings.advanced?14:66);
                show(page_hint,24,common_height-32,body_width-62,24);SetWindowTextW(page_hint,settings.advanced?L"Размытие применяется ко всем трём островкам.":L"Заливка и размытие общие для всех островков.");
                const int toggle_top=common_height+14;card(toggle_top,88);show(advanced,24,toggle_top+14,body_width-62,28);show(advanced_hint,24,toggle_top+49,body_width-62,30);
                SetWindowTextW(advanced_hint,settings.advanced?L"Для каждого островка доступны свой радиус и заливка.":L"Выключено — используются общие радиус и заливка.");extent=toggle_top+104;
                if(settings.advanced){
                    const int start=toggle_top+102;
                    for(int index=0;index<3;++index){const int top=start+index*108;card(top,96);show(island_labels[index],24,top+12,body_width-62,22);const int part=(body_width-72)/2;
                        for(int column=0;column<2;++column){const int slot=(column?7:4)+index,x=24+column*(part+16);show(sliders[slot].label,x,top+40,part-55,20);show(sliders[slot].value,x+part-54,top+40,54,20);show(sliders[slot].track,x-5,top+62,part+8,28);}
                    }extent=start+318;
                }
            }
        }else if(page==2){
            card(0,102);show(preview,24,15,212,34);show(popup_hint,24,59,body_width-62,36);
            card(116,272);row(10,130);row(12,182);row(11,234);row(14,286);show(popup_outline,24,346,body_width-62,26);
            card(402,110);show(visualizer,24,416,body_width-62,28);show(visualizer_hint,24,453,body_width-62,46);extent=528;
        }else if(page==3){
            card(0,140);show(about,24,18,body_width-62,36);show(about_hint,24,65,body_width-62,56);card(154,118);show(autostart,24,170,body_width-62,28);show(folder,24,214,252,34);extent=288;
        }else{
            card(0,312);show(browser_label,24,18,body_width-62,46);show(browser_hint,24,72,body_width-62,70);show(browser_connect,24,152,260,34);show(browser_check,24,202,160,34);show(browser_disable,196,202,140,34);show(browser_developer,24,254,330,34);extent=328;
        }
        if(page==4 && IsWindowVisible(window))SetTimer(window,browser_timer,2000,nullptr);else KillTimer(window,browser_timer);if(page==4)browser_status();
        if(page==0)SetWindowTextW(mode_label,L"Режим панели");
        scroll=std::clamp(scroll,0,std::max(0,extent-body_height));
        SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,extent-1,static_cast<UINT>(body_height),scroll,0};SetScrollInfo(body,SB_VERT,&info,TRUE);
        GetClientRect(body,&content);if(MulDiv(content.right,96,dpi)!=body_width){arranging=false;layout(false);return;}
        std::array<bool,512> visible{};for(const auto& item:placements){const int id=GetDlgCtrlID(item.window);if(id>=0 && id<static_cast<int>(visible.size()))visible[id]=true;}
        for(auto child=GetWindow(body,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){const int id=GetDlgCtrlID(child);if(id<0 || id>=static_cast<int>(visible.size()) || !visible[id])ShowWindow(child,SW_HIDE);}
        place_content(true);InvalidateRect(window,nullptr,FALSE);for(const auto item:navigation)InvalidateRect(item,nullptr,FALSE);arranging=false;
    }
    void browser_status(){
        if(!browser_label)return;std::wstring label;
        if(browser_open_until){
            if(islands::browser::event_signalled(L"Local\\Stillbar.Yandex.ExtensionsOpened.v1")){browser_open_until=0;browser_error.clear();SetWindowTextW(browser_hint,L"Яндекс подтвердил создание вкладки со страницей расширений.");}
            else if(islands::browser::event_signalled(L"Local\\Stillbar.Yandex.ExtensionsFailed.v1") || GetTickCount64()>=browser_open_until){browser_open_until=0;browser_error=L"Яндекс не подтвердил открытие страницы расширений.";}
        }
        if(!browser_error.empty())label=L"Ошибка: "+browser_error;
        else if(!islands::browser::prepared())label=L"Не подключено";
        else if(islands::browser::connected())label=L"Подключено — обмен с расширением установлен";
        else label=L"Ожидает подключения расширения";
        const bool connected=islands::browser::connected(),published=islands::browser::store_available();
        SetWindowTextW(browser_connect,connected?L"Открыть страницу расширений":L"Установить расширение");EnableWindow(browser_connect,connected || published);
        if(!browser_open_until && browser_error.empty() && !connected)SetWindowTextW(browser_hint,published?L"Откроется страница Stillbar Media в каталоге. Подтвердите добавление — подключение произойдёт автоматически.":L"Расширение пока не опубликовано. Обычная установка из каталога ещё недоступна; режим разработчика предназначен только для тестирования.");
        SetWindowTextW(browser_label,label.c_str());EnableWindow(browser_disable,islands::browser::prepared());
    }
    void prepare_browser(){
        browser_error.clear();
        if(islands::browser::connected()){
            if(islands::browser::open_extension_page(window)){browser_open_until=GetTickCount64()+6000;SetWindowTextW(browser_hint,L"Ожидание ответа Яндекса об открытии страницы…");}
            else browser_error=L"подключённый компонент не поддерживает открытие страницы. Обновите интеграцию.";
            browser_status();return;
        }
        const auto directory=std::filesystem::path(islands::program_path()).parent_path();
        const auto result=islands::browser::prepare_store(directory);
        if(result!=ERROR_SUCCESS)browser_error=L"обычная установка недоступна: расширение ещё не опубликовано либо комплект повреждён.";
        else if(!islands::browser::open_store(window))browser_error=L"не удалось открыть страницу расширения в Яндексе.";
        browser_status();
    }
    void prepare_browser_developer(){
        browser_error.clear();const auto directory=std::filesystem::path(islands::program_path()).parent_path();const auto result=islands::browser::prepare(directory);
        if(result!=ERROR_SUCCESS){browser_error=L"не удалось подготовить компонент ("+std::to_wstring(result)+L"). Проверьте комплект и доступ к файлам.";browser_status();return;}
        const bool copied=islands::browser::copy_extension_path(window);const bool opened=islands::browser::open_extension_page(window);
        const auto extension=islands::browser::extension_path().wstring();
        const auto instructions=std::wstring(L"В Яндекс Браузере включите «Режим разработчика» и нажмите «Загрузить распакованное расширение». ")+(copied?L"Путь к постоянной папке уже скопирован: вставьте его в окне выбора папки. ":L"Выберите папку:\n"+extension+L"\n")+L"\nЕсли тестовое расширение загружено из другой папки, удалите его перед первой загрузкой новой папки. Для последующих обновлений используйте кнопку «Применить обновление расширения» в его значке.\n\nОбновите вкладки с медиа. Расширение ещё не опубликовано; Яндекс может отключать его после перезапуска."+(opened?L"\n\nЗапрос на открытие страницы отправлен подключённому расширению.":L"\n\nЕсли страница расширений не открылась, вставьте browser://extensions в адресную строку Яндекса и нажмите Enter. Внешний запуск этой страницы на проверенной версии Яндекса не сработал.");
        MessageBoxW(window,instructions.c_str(),L"Stillbar Media — тестовое подключение",MB_OK|MB_ICONINFORMATION);browser_status();
    }
    void disable_browser(){
        browser_error.clear();const auto result=islands::browser::disable();if(result!=ERROR_SUCCESS && result!=ERROR_FILE_NOT_FOUND)browser_error=L"не удалось отключить компонент ("+std::to_wstring(result)+L"). Повторите попытку.";browser_status();
    }
    void scroll_page(int action,int wheel=0){
        int value=scroll;if(wheel)value-=wheel*48/WHEEL_DELTA;else switch(action){case SB_LINEUP:value-=32;break;case SB_LINEDOWN:value+=32;break;case SB_PAGEUP:value-=body_height-32;break;case SB_PAGEDOWN:value+=body_height-32;break;case SB_TOP:value=0;break;case SB_BOTTOM:value=extent;break;case SB_THUMBTRACK:{SCROLLINFO info{sizeof(info),SIF_TRACKPOS};GetScrollInfo(body,SB_VERT,&info);value=info.nTrackPos;break;}default:return;}
        value=std::clamp(value,0,std::max(0,extent-body_height));if(value==scroll)return;scroll=value;SetScrollPos(body,SB_VERT,scroll,TRUE);place_content();
    }
    void save_window_size(){
        if(IsZoomed(window) || IsIconic(window))return;RECT rect{};GetClientRect(window,&rect);const auto path=islands::config_path();
        const auto width=std::to_wstring(MulDiv(rect.right,96,dpi)),height=std::to_wstring(MulDiv(rect.bottom,96,dpi));WritePrivateProfileStringW(L"Window",L"Width",width.c_str(),path.c_str());WritePrivateProfileStringW(L"Window",L"Height",height.c_str(),path.c_str());
    }
    void reveal_control(HWND target){
        const auto found=std::find_if(placements.begin(),placements.end(),[&](const Placement& item){return item.window==target;});if(found==placements.end())return;
        int value=scroll;if(found->y<scroll+10)value=found->y-10;else if(found->y+found->height>scroll+body_height-10)value=found->y+found->height-body_height+10;
        value=std::clamp(value,0,std::max(0,extent-body_height));if(value!=scroll){scroll=value;SetScrollPos(body,SB_VERT,scroll,TRUE);place_content();}
    }
    void paint(HWND target,bool content){
        PAINTSTRUCT paint{};const auto dc=BeginPaint(target,&paint);RECT area{};GetClientRect(target,&area);const auto memory=CreateCompatibleDC(dc);const auto bitmap=CreateCompatibleBitmap(dc,std::max(1L,area.right),std::max(1L,area.bottom));const auto old_bitmap=SelectObject(memory,bitmap);islands::modern::fill(memory,area,palette.background);
        if(content){for(auto rect:cards){rect.left=px(rect.left);rect.right=px(rect.right);rect.top=px(rect.top-scroll);rect.bottom=px(rect.bottom-scroll);islands::modern::rounded(memory,rect,px(8),palette.card,palette.line);}}
        else{const int width=MulDiv(area.right,96,dpi),side=width<820?184:216;RECT bar{0,0,px(side),area.bottom};islands::modern::fill(memory,bar,palette.sidebar);RECT edge{px(side)-1,0,px(side),area.bottom};islands::modern::fill(memory,edge,palette.line);if(icon)DrawIconEx(memory,px(20),px(29),icon,px(28),px(28),0,nullptr,DI_NORMAL);const auto old_font=SelectObject(memory,font);SetBkMode(memory,TRANSPARENT);SetTextColor(memory,palette.text);RECT label{px(58),px(30),px(side-8),px(52)};DrawTextW(memory,L"Stillbar",-1,&label,DT_SINGLELINE|DT_VCENTER);SetTextColor(memory,palette.muted);label={px(22),px(67),px(side-8),px(86)};const auto version=version_text();DrawTextW(memory,version.c_str(),-1,&label,DT_SINGLELINE);SelectObject(memory,old_font);}
        BitBlt(dc,0,0,area.right,area.bottom,memory,0,0,SRCCOPY);SelectObject(memory,old_bitmap);DeleteObject(bitmap);DeleteDC(memory);EndPaint(target,&paint);
    }
