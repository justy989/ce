#pragma once

#include "ce.h"

struct CeLayout_t;

typedef enum{
     CE_LAYOUT_TYPE_VIEW,
     CE_LAYOUT_TYPE_LIST,
     CE_LAYOUT_TYPE_TAB,
     CE_LAYOUT_TYPE_TAB_LIST,
}CeLayoutType_t;

typedef struct{
     struct CeLayout_t** layouts;
     int64_t layout_count;
     bool vertical;
     CeRect_t rect;
}CeLayoutList_t;

typedef struct{
     struct CeLayout_t* root;
     struct CeLayout_t* current;
     CeRect_t rect;
}CeTabLayout_t;

typedef struct{
     CeTabLayout_t* tabs;
     int64_t tab_count;
     CeRect_t rect;
}CeTabListLayout_t;

typedef struct CeLayout_t{
     CeLayoutType_t type;

     union{
          CeView_t view;
          CeLayoutList_t list;
          CeTabLayout_t tab;
          CeTabListLayout_t tab_list;
     };
}CeLayout_t;

CeLayout_t* ce_layout_tab_init(CeBuffer_t* buffer);
bool ce_layout_tab_split(CeLayout_t* layout, bool vertical);
CeLayout_t* ce_layout_find_at(CeLayout_t* layout, CePoint_t point);
void ce_layout_distribute_rect(CeLayout_t* layout, CeRect_t rect);
