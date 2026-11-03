#include <stdbool.h>
#include <raylib.h>
#include "views/authView.h"
#include "views/chrtView.h"
#include "views/imageView.h"
#include "views/mapinfoView.h"
#include "views/mt5View.h"
#include "views/sndView.h"
#include "views/xwmView.h"
#include "views/views.h"
#include "ui/toolbar.h"

typedef struct {
    void    (*close)(void);
    bool    (*isOpen)(void);
    void    (*draw)(Rectangle area);
    void    (*toolbar)(void);   // Adds the actions of the view to the toolbar, NULL if it has none
    void    (*status)(ViewStatus* status);
} View;

static const View g_views[VIEW_COUNT] = {
    [VIEW_IMAGE] = { ImageView_Close, ImageView_IsOpen, ImageView_Draw, NULL, ImageView_Status },
    [VIEW_MT5]   = { MT5View_Close, MT5View_IsOpen, MT5View_Draw, MT5View_Toolbar, MT5View_Status },
    [VIEW_XWM]   = { XWMView_Close, XWMView_IsOpen, XWMView_Draw, XWMView_Toolbar, XWMView_Status },
    [VIEW_SND]   = { SNDView_Close, SNDView_IsOpen, SNDView_Draw, SNDView_Toolbar, SNDView_Status },
    [VIEW_AUTH]  = { AUTHView_Close, AUTHView_IsOpen, AUTHView_Draw, AUTHView_Toolbar, AUTHView_Status },
    [VIEW_CHRT]  = { CHRTView_Close, CHRTView_IsOpen, CHRTView_Draw, CHRTView_Toolbar, CHRTView_Status },
    [VIEW_MAPINFO] = { MAPINFOView_Close, MAPINFOView_IsOpen, MAPINFOView_Draw, MAPINFOView_Toolbar, MAPINFOView_Status },
};

void Views_CloseOthers(ViewID view) {
    for (int i = 0; i < VIEW_COUNT; i++) {
        if (i != view)
            g_views[i].close();
    }
}

void Views_CloseAll(void) {
    Views_CloseOthers(VIEW_NONE);
}

void Views_Draw(Rectangle area) {
    for (int i = 0; i < VIEW_COUNT; i++) {
        if (g_views[i].isOpen()) {
            g_views[i].draw(area);
            return;
        }
    }
}

void Views_Toolbar(void) {
    for (int i = 0; i < VIEW_COUNT; i++) {
        if (g_views[i].isOpen()) {
            if (g_views[i].toolbar != NULL) {
                Toolbar_Separator();
                g_views[i].toolbar();
            }
            return;
        }
    }
}

bool Views_Status(ViewStatus* status) {
    *status = (ViewStatus) { 0 };
    for (int i = 0; i < VIEW_COUNT; i++) {
        if (g_views[i].isOpen()) {
            g_views[i].status(status);
            return true;
        }
    }
    return false;
}
