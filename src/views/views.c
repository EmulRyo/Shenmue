#include <stdbool.h>
#include <raylib.h>
#include "views/imageView.h"
#include "views/mt5View.h"
#include "views/sndView.h"
#include "views/xwmView.h"
#include "views/views.h"

typedef struct {
    void    (*close)(void);
    bool    (*isOpen)(void);
    void    (*draw)(Rectangle area);
} View;

static const View g_views[VIEW_COUNT] = {
    [VIEW_IMAGE] = { ImageView_Close, ImageView_IsOpen, ImageView_Draw },
    [VIEW_MT5]   = { MT5View_Close, MT5View_IsOpen, MT5View_Draw },
    [VIEW_XWM]   = { XWMView_Close, XWMView_IsOpen, XWMView_Draw },
    [VIEW_SND]   = { SNDView_Close, SNDView_IsOpen, SNDView_Draw },
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
