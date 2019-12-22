#include "ce_layout.h"
#include "ce_app.h"

#include <stdlib.h>
#include <assert.h>

CeLayout_t* ce_layout_tab_list_init(CeLayout_t* tab_layout){
     CeLayout_t* tab_list_layout = calloc(1, sizeof(*tab_list_layout));
     if(!tab_list_layout) return NULL;
     tab_list_layout->type = CE_LAYOUT_TYPE_TAB_LIST;
     tab_list_layout->tab_list.tabs = malloc(sizeof(*tab_list_layout->tab_list.tabs));
     tab_list_layout->tab_list.tabs[0] = tab_layout;
     tab_list_layout->tab_list.tab_count = 1;
     tab_list_layout->tab_list.current = tab_layout;
     return tab_list_layout;
}

static void ce_layout_resize_for_tabline(CeLayout_t* layout){
     switch(layout->type){
     default:
          break;
     case CE_LAYOUT_TYPE_VIEW:
          if(layout->view.rect.top == 0){
               layout->view.rect.top = 1;
          }
          break;
     case CE_LAYOUT_TYPE_LIST:
     {
          if(layout->list.rect.top == 0){
               layout->list.rect.top = 1;
          }

          for(int64_t i = 0; i < layout->list.layout_count; i++){
              ce_layout_resize_for_tabline(layout->list.layouts[i]);
          }
     } break;
     case CE_LAYOUT_TYPE_TAB:
          if(layout->tab.rect.top == 0){
               layout->tab.rect.top = 1;
          }
          ce_layout_resize_for_tabline(layout->tab.root);
          break;
     case CE_LAYOUT_TYPE_TAB_LIST:
          if(layout->tab_list.rect.top == 0){
               layout->tab_list.rect.top = 1;
          }
          for(int64_t i = 0; i < layout->tab_list.tab_count; i++){
               ce_layout_resize_for_tabline(layout->tab_list.tabs[i]);
          }
          break;
     }
}

CeLayout_t* ce_layout_tab_list_add(CeLayout_t* tab_list_layout){
     assert(tab_list_layout->type == CE_LAYOUT_TYPE_TAB_LIST);
     CeRect_t rect = tab_list_layout->tab_list.rect;
     int64_t new_tab_count = tab_list_layout->tab_list.tab_count + 1;
     CeLayout_t** new_tabs = realloc(tab_list_layout->tab_list.tabs, new_tab_count * sizeof(*tab_list_layout->tab_list.tabs));
     if(!new_tabs) return false;
     tab_list_layout->tab_list.tabs = new_tabs;
     new_tabs[tab_list_layout->tab_list.tab_count] = ce_layout_tab_init(tab_list_layout->tab_list.current->tab.current->view.buffer, rect);
     tab_list_layout->tab_list.tab_count = new_tab_count;
     for(int64_t i = 0; i < new_tab_count; i++){
         ce_layout_resize_for_tabline(new_tabs[i]);
     }
     return new_tabs[tab_list_layout->tab_list.tab_count - 1];
}

CeLayout_t* ce_layout_view_init(CeBuffer_t* buffer){
     CeLayout_t* view_layout = calloc(1, sizeof(*view_layout));
     if(!view_layout) return NULL;
     view_layout->type = CE_LAYOUT_TYPE_VIEW;
     view_layout->view.buffer = buffer;
     view_layout->view.user_data = calloc(1, sizeof(CeAppViewData_t));
     return view_layout;
}

CeLayout_t* ce_layout_tab_init(CeBuffer_t* buffer, CeRect_t rect){
     CeLayout_t* view_layout = ce_layout_view_init(buffer);
     view_layout->view.rect = rect;

     CeLayout_t* tab_layout = calloc(1, sizeof(*tab_layout));
     if(!tab_layout){
         free(view_layout);
         return NULL;
     }
     tab_layout->type = CE_LAYOUT_TYPE_TAB;
     tab_layout->tab.root = view_layout;
     tab_layout->tab.current = view_layout;
     return tab_layout;
}

void ce_layout_free(CeLayout_t** root){
     CeLayout_t* layout = *root;
     switch(layout->type){
     default:
          break;
     case CE_LAYOUT_TYPE_VIEW:
          free(layout->view.user_data);
          layout->view.user_data = NULL;
          break;
     case CE_LAYOUT_TYPE_LIST:
          for(int64_t i = 0; i < layout->list.layout_count; i++){
               if(layout->list.layouts[i] != NULL){
                    ce_layout_free(&layout->list.layouts[i]);
               }
          }
          free(layout->list.layouts);
          layout->list.layout_count = 0;
          break;
     case CE_LAYOUT_TYPE_TAB:
          ce_layout_free(&layout->tab.root);
          break;
     case CE_LAYOUT_TYPE_TAB_LIST:
          for(int64_t i = 0; i < layout->tab_list.tab_count; i++){
               ce_layout_free(&layout->tab_list.tabs[i]);
          }
          free(layout->tab_list.tabs);
          layout->tab_list.tab_count = 0;
          break;
     }

     free(*root);
     *root = NULL;
}

static CeBuffer_t* ce_layout_find_buffer(CeLayout_t* layout){
     switch(layout->type){
     default:
          break;
     case CE_LAYOUT_TYPE_VIEW:
          if(layout->view.buffer) return layout->view.buffer;
          break;
     case CE_LAYOUT_TYPE_LIST:
          for(int64_t i = 0; i < layout->list.layout_count; i++){
               CeBuffer_t* found = ce_layout_find_buffer(layout->list.layouts[i]);
               if(found) return found;
          }
          break;
     case CE_LAYOUT_TYPE_TAB:
          return ce_layout_find_buffer(layout->tab.root);
     case CE_LAYOUT_TYPE_TAB_LIST:
          for(int64_t i = 0; i < layout->tab_list.tab_count; i++){
               CeBuffer_t* buffer = ce_layout_find_buffer(layout->tab_list.tabs[i]);
               if(buffer) return buffer;
          }
          break;
     }

     return NULL;
}

static CeRect_t ce_layout_rect(CeLayout_t* layout){
     if(layout->type == CE_LAYOUT_TYPE_VIEW) return layout->view.rect;

     CeRect_t result = {};

     if(layout->type == CE_LAYOUT_TYPE_LIST){
          if(layout->list.layout_count <= 0) return result;
          result = ce_layout_rect(layout->list.layouts[0]);

          for(int64_t i = 1; i < layout->list.layout_count; i++){
               CeRect_t rect = ce_layout_rect(layout->list.layouts[i]);
               if(rect.left < result.left) result.left = rect.left;
               if(rect.right > result.right) result.right = rect.right;
               if(rect.top < result.top) result.top = rect.top;
               if(rect.bottom > result.bottom) result.bottom = rect.bottom;
          }

          return result;
     }

     return result;
}

CeLayout_t* ce_layout_split(CeLayout_t* layout, bool vertical){
     assert(layout->type == CE_LAYOUT_TYPE_TAB);
     CeLayout_t* parent_of_current = ce_layout_find_parent(layout, layout->tab.current);
     if(parent_of_current){
          CeBuffer_t* buffer = ce_layout_find_buffer(layout->tab.current);
          assert(buffer);
          switch(parent_of_current->type){
          default:
               break;
          case CE_LAYOUT_TYPE_LIST:
               if(parent_of_current->list.vertical == vertical){
                    CeRect_t parent_layout_rect = ce_layout_rect(parent_of_current);
                    CeLayout_t* new_layout = ce_layout_view_init(buffer);
                    if(!new_layout) return NULL;
                    new_layout->view.scroll = layout->tab.current->view.scroll;
                    new_layout->view.cursor = layout->tab.current->view.cursor;

                    int64_t split_layout_index = 0;
                    for(int64_t i = 0; i < parent_of_current->list.layout_count; i++){
                         if(parent_of_current->list.layouts[i] == layout->tab.current){
                              split_layout_index = i;
                              break;
                         }
                    }

                    ce_log("split layout index: %d\n", split_layout_index);

                    int64_t new_layout_count = parent_of_current->list.layout_count + 1;
                    parent_of_current->list.layouts = realloc(parent_of_current->list.layouts,
                                                              new_layout_count * sizeof(*parent_of_current->list.layouts));
                    if(parent_of_current->list.layouts){
                         parent_of_current->list.layout_count = new_layout_count;
                    }else{
                         return NULL;
                    }

                    // shift down the rest of the layouts first
                    for(int64_t i = parent_of_current->list.layout_count - 1; i > split_layout_index; i--){
                         parent_of_current->list.layouts[i] = parent_of_current->list.layouts[i - 1];
                    }
                    parent_of_current->list.layouts[split_layout_index] = new_layout;
                    ce_layout_distribute_rect(parent_of_current, parent_layout_rect);
                    layout->tab.current = new_layout;
                    return new_layout;
               }else{
                    CeLayout_t* new_list_layout = calloc(1, sizeof(*new_list_layout));
                    if(!new_list_layout) return NULL;
                    new_list_layout->type = CE_LAYOUT_TYPE_LIST;
                    new_list_layout->list.layouts = malloc(sizeof(*new_list_layout->list.layouts));
                    if(!new_list_layout->list.layouts) return NULL;
                    new_list_layout->list.layout_count = 1;
                    new_list_layout->list.vertical = vertical;
                    new_list_layout->list.layouts[0] = layout->tab.current;

                    // point parent at new list
                    for(int64_t i = 0; i < parent_of_current->list.layout_count; i++){
                         if(parent_of_current->list.layouts[i] == layout->tab.current){
                              parent_of_current->list.layouts[i] = new_list_layout;
                              break;
                         }
                    }

                    return ce_layout_split(layout, vertical);
               }
               break;
          case CE_LAYOUT_TYPE_TAB:
          {
               CeLayout_t* list_layout = calloc(1, sizeof(*list_layout));
               list_layout->type = CE_LAYOUT_TYPE_LIST;
               list_layout->list.layout_count = 1;
               list_layout->list.layouts = calloc(list_layout->list.layout_count, sizeof(*list_layout->list.layouts));
               list_layout->list.layouts[0] = layout->tab.current;
               list_layout->list.vertical = vertical;

               parent_of_current->tab.root = list_layout;

               return ce_layout_split(layout, vertical);
          } break;
          case CE_LAYOUT_TYPE_TAB_LIST:
               if(layout->tab_list.current) return ce_layout_split(layout->tab_list.current, vertical);
               break;
          }
     }

     return NULL;
}

void ce_layout_distribute_rect(CeLayout_t* layout, CeRect_t rect){
     switch(layout->type){
     default:
          break;
     case CE_LAYOUT_TYPE_VIEW:
          layout->view.rect = rect;
          break;
     case CE_LAYOUT_TYPE_LIST:
     {
          layout->list.rect = rect;
          CeRect_t sliced_rect = rect;
          int64_t separator_lines = layout->list.layout_count - 1;

          if(layout->list.vertical){
               assert(layout->list.layout_count != 0);
               int64_t rect_height = rect.bottom - rect.top;
               int64_t slice_height = (rect_height - separator_lines) / layout->list.layout_count;
               int64_t leftover_lines = (rect_height - separator_lines) % layout->list.layout_count;
               sliced_rect.bottom = sliced_rect.top + slice_height;

               for(int64_t i = 0; i < layout->list.layout_count; i++){
                    if(leftover_lines > 0){
                         leftover_lines--;
                         sliced_rect.bottom++;
                    }

                    ce_layout_distribute_rect(layout->list.layouts[i], sliced_rect);

                    sliced_rect.top = sliced_rect.bottom + 1;
                    sliced_rect.bottom = sliced_rect.top + slice_height;
               }
          }else{
               assert(layout->list.layout_count != 0);
               int64_t rect_width = rect.right - rect.left;
               int64_t slice_width = (rect_width - separator_lines) / layout->list.layout_count;
               int64_t leftover_lines = (rect_width - separator_lines) % layout->list.layout_count;
               sliced_rect.right = sliced_rect.left + slice_width;

               for(int64_t i = 0; i < layout->list.layout_count; i++){
                    if(leftover_lines > 0){
                         leftover_lines--;
                         sliced_rect.right++;
                    }

                    ce_layout_distribute_rect(layout->list.layouts[i], sliced_rect);

                    sliced_rect.left = sliced_rect.right + 1;
                    sliced_rect.right = sliced_rect.left + slice_width;
               }
          }
     } break;
     case CE_LAYOUT_TYPE_TAB:
          layout->tab.rect = rect;
          ce_layout_distribute_rect(layout->tab.root, rect);
          break;
     case CE_LAYOUT_TYPE_TAB_LIST:
          layout->tab_list.rect = rect;
          if(layout->tab_list.tab_count > 1) rect.top++; // leave space for a tab bar, this probably doesn't want to be built in?
          ce_layout_distribute_rect(layout->tab_list.current, rect);
          break;
     }
}

typedef struct{
     int64_t new_value;
     int64_t adjacent_old_value;
     int64_t adjacent_new_value;
} ResizeRectResult_t;

// returns the new adjacent edge
static ResizeRectResult_t resize_rect(CeRect_t* rect, CeRect_t bounds, CeDirection_t direction, bool expand, int64_t amount){
     ResizeRectResult_t result = {-1, -1, -1};

     switch(direction){
     default:
          break;
     case CE_LEFT:
     {
          int64_t new_left = rect->left;

          if(expand){
               new_left -= amount;
          }else{
               new_left += amount;
          }

          if(new_left < bounds.left || new_left > bounds.right) return result;

          int64_t new_adjacent = new_left - 1;
          if(new_adjacent < bounds.left || new_adjacent > bounds.right) return result;

          result.new_value = new_left;
          result.adjacent_old_value = rect->left - 1;
          result.adjacent_new_value = new_adjacent;
          return result;
     } break;
     case CE_RIGHT:
     {
          int64_t new_right = rect->right;

          if(expand){
               new_right += amount;
          }else{
               new_right -= amount;
          }

          if(new_right < bounds.left || new_right > bounds.right) return result;

          int64_t new_adjacent = new_right + 1;
          if(new_adjacent < bounds.left || new_adjacent > bounds.right) return result;

          result.new_value = new_right;
          result.adjacent_old_value = rect->right + 1;
          result.adjacent_new_value = new_adjacent;
          return result;
     } break;
     case CE_UP:
     {
          int64_t new_top = rect->top;

          if(expand){
               new_top -= amount;
          }else{
               new_top += amount;
          }

          if(new_top < bounds.top || new_top > bounds.bottom) return result;

          int64_t new_adjacent = new_top - 1;
          if(new_adjacent < bounds.top || new_adjacent > bounds.bottom) return result;

          result.new_value = new_top;
          result.adjacent_old_value = rect->top - 1;
          result.adjacent_new_value = new_adjacent;
          return result;
     } break;
     case CE_DOWN:
     {
          int64_t new_bottom = rect->bottom;

          if(expand){
               new_bottom += amount;
          }else{
               new_bottom -= amount;
          }

          if(new_bottom < bounds.top || new_bottom > bounds.bottom) return result;

          int64_t new_adjacent = new_bottom + 1;
          if(new_adjacent < bounds.top || new_adjacent > bounds.bottom) return result;

          result.new_value = new_bottom;
          result.adjacent_old_value = rect->bottom + 1;
          result.adjacent_new_value = new_adjacent;
          return result;
     } break;
     }

     return result;
}

static void resize_opposites_left(CeLayout_t* root, CeLayout_t* layout, CeRect_t rect, ResizeRectResult_t* result){
    int64_t bottom_start = layout->view.rect.top;

    for(int64_t i = layout->view.rect.top - 1; i >= rect.top; i--){
         CePoint_t point = {result->adjacent_old_value, i};
         CeLayout_t* adjacent_layout = ce_layout_find_at(root, point);
         if(adjacent_layout && adjacent_layout->view.rect.right == result->adjacent_old_value){
              adjacent_layout->view.rect.right = result->adjacent_new_value;
              i = adjacent_layout->view.rect.top;
              if((adjacent_layout->view.rect.bottom + 1) > bottom_start) bottom_start = adjacent_layout->view.rect.bottom + 1;
         }else{
              break;
         }
    }

    for(int64_t i = bottom_start; i <= rect.bottom; i++){
         CePoint_t point = {result->adjacent_old_value, i};
         CeLayout_t* adjacent_layout = ce_layout_find_at(root, point);
         if(adjacent_layout && adjacent_layout->view.rect.right == result->adjacent_old_value){
              adjacent_layout->view.rect.right = result->adjacent_new_value;
              i = adjacent_layout->view.rect.bottom;
         }else{
              break;
         }
    }
}

static void resize_shareds_left(CeLayout_t* root, CeLayout_t* layout, CeRect_t rect, ResizeRectResult_t* result){
    for(int64_t i = layout->view.rect.top - 1; i >= rect.top; i--){
         CePoint_t point = {layout->view.rect.left, i};
         CeLayout_t* adjacent_layout = ce_layout_find_at(root, point);
         if(adjacent_layout && adjacent_layout->view.rect.left == layout->view.rect.left){
              adjacent_layout->view.rect.left = result->new_value;
              i = adjacent_layout->view.rect.top;
         }else{
              break;
         }
    }

    for(int64_t i = layout->view.rect.bottom + 1; i <= rect.bottom; i++){
         CePoint_t point = {layout->view.rect.left, i};
         CeLayout_t* adjacent_layout = ce_layout_find_at(root, point);
         if(adjacent_layout && adjacent_layout->view.rect.left == layout->view.rect.left){
              adjacent_layout->view.rect.left = result->new_value;
              i = adjacent_layout->view.rect.bottom;
         }else{
              break;
         }
    }
}

static void resize_opposites_right(CeLayout_t* root, CeLayout_t* layout, CeRect_t rect, ResizeRectResult_t* result){
    int64_t bottom_start = layout->view.rect.top;
    for(int64_t i = layout->view.rect.top - 1; i >= rect.top; i--){
         CePoint_t point = {result->adjacent_old_value, i};
         CeLayout_t* adjacent_layout = ce_layout_find_at(root, point);
         if(adjacent_layout && adjacent_layout->view.rect.left == result->adjacent_old_value){
              adjacent_layout->view.rect.left = result->adjacent_new_value;
              i = adjacent_layout->view.rect.top;
              if((adjacent_layout->view.rect.bottom + 1) > bottom_start) bottom_start = adjacent_layout->view.rect.bottom + 1;
         }else{
              break;
         }
    }

    for(int64_t i = bottom_start; i <= rect.bottom; i++){
         CePoint_t point = {result->adjacent_old_value, i};
         CeLayout_t* adjacent_layout = ce_layout_find_at(root, point);
         if(adjacent_layout && adjacent_layout->view.rect.left == result->adjacent_old_value){
              adjacent_layout->view.rect.left = result->adjacent_new_value;
              i = adjacent_layout->view.rect.bottom;
         }else{
              break;
         }
    }
}

static void resize_shareds_right(CeLayout_t* root, CeLayout_t* layout, CeRect_t rect, ResizeRectResult_t* result){
    for(int64_t i = layout->view.rect.top - 1; i >= rect.top; i--){
         CePoint_t point = {layout->view.rect.right, i};
         CeLayout_t* adjacent_layout = ce_layout_find_at(root, point);
         if(adjacent_layout && adjacent_layout->view.rect.right == layout->view.rect.right){
              adjacent_layout->view.rect.right = result->new_value;
              i = adjacent_layout->view.rect.top;
         }else{
              break;
         }
    }

    for(int64_t i = layout->view.rect.bottom + 1; i <= rect.bottom; i++){
         CePoint_t point = {layout->view.rect.right, i};
         CeLayout_t* adjacent_layout = ce_layout_find_at(root, point);
         if(adjacent_layout && adjacent_layout->view.rect.right == layout->view.rect.right){
              adjacent_layout->view.rect.right = result->new_value;
              i = adjacent_layout->view.rect.bottom;
         }else{
              break;
         }
    }
}

static void resize_opposites_up(CeLayout_t* root, CeLayout_t* layout, CeRect_t rect, ResizeRectResult_t* result){
    int64_t right_start = layout->view.rect.left;

    for(int64_t i = layout->view.rect.left - 1; i >= rect.left; i--){
         CePoint_t point = {i, result->adjacent_old_value};
         CeLayout_t* adjacent_layout = ce_layout_find_at(root, point);
         if(adjacent_layout && adjacent_layout->view.rect.bottom == result->adjacent_old_value){
              adjacent_layout->view.rect.bottom = result->adjacent_new_value;
              i = adjacent_layout->view.rect.left;
              if((adjacent_layout->view.rect.right + 1) > right_start) right_start = adjacent_layout->view.rect.right + 1;
         }else{
              break;
         }
    }

    for(int64_t i = right_start; i <= rect.right; i++){
         CePoint_t point = {i, result->adjacent_old_value};
         CeLayout_t* adjacent_layout = ce_layout_find_at(root, point);
         if(adjacent_layout && adjacent_layout->view.rect.bottom == result->adjacent_old_value){
              adjacent_layout->view.rect.bottom = result->adjacent_new_value;
              i = adjacent_layout->view.rect.right;
         }else{
              break;
         }
    }
}

static void resize_shareds_up(CeLayout_t* root, CeLayout_t* layout, CeRect_t rect, ResizeRectResult_t* result){
    for(int64_t i = layout->view.rect.left - 1; i >= rect.left; i--){
         CePoint_t point = {i, layout->view.rect.top};
         CeLayout_t* adjacent_layout = ce_layout_find_at(root, point);
         if(adjacent_layout && adjacent_layout->view.rect.top == layout->view.rect.top){
              adjacent_layout->view.rect.top = result->new_value;
              i = adjacent_layout->view.rect.left;
         }else{
              break;
         }
    }

    for(int64_t i = layout->view.rect.right + 1; i <= rect.right; i++){
         CePoint_t point = {i, layout->view.rect.top};
         CeLayout_t* adjacent_layout = ce_layout_find_at(root, point);
         if(adjacent_layout && adjacent_layout->view.rect.top == layout->view.rect.top){
              adjacent_layout->view.rect.top = result->new_value;
              i = adjacent_layout->view.rect.right;
         }else{
              break;
         }
    }
}

static void resize_opposites_down(CeLayout_t* root, CeLayout_t* layout, CeRect_t rect, ResizeRectResult_t* result){
    int64_t right_start = layout->view.rect.left;

    for(int64_t i = layout->view.rect.left - 1; i >= rect.left; i--){
         CePoint_t point = {i, result->adjacent_old_value};
         CeLayout_t* adjacent_layout = ce_layout_find_at(root, point);
         if(adjacent_layout && adjacent_layout->view.rect.top == result->adjacent_old_value){
              adjacent_layout->view.rect.top = result->adjacent_new_value;
              i = adjacent_layout->view.rect.left;
              if((adjacent_layout->view.rect.right + 1) > right_start) right_start = adjacent_layout->view.rect.right + 1;
         }else{
              break;
         }
    }

    for(int64_t i = right_start; i <= rect.right; i++){
         CePoint_t point = {i, result->adjacent_old_value};
         CeLayout_t* adjacent_layout = ce_layout_find_at(root, point);
         if(adjacent_layout && adjacent_layout->view.rect.top == result->adjacent_old_value){
              adjacent_layout->view.rect.top = result->adjacent_new_value;
              i = adjacent_layout->view.rect.right;
         }else{
              break;
         }
    }
}

static void resize_shareds_down(CeLayout_t* root, CeLayout_t* layout, CeRect_t rect, ResizeRectResult_t* result){
    for(int64_t i = layout->view.rect.left - 1; i >= rect.left; i--){
         CePoint_t point = {i, layout->view.rect.bottom};
         CeLayout_t* adjacent_layout = ce_layout_find_at(root, point);
         if(adjacent_layout && adjacent_layout->view.rect.bottom == layout->view.rect.bottom){
              adjacent_layout->view.rect.bottom = result->new_value;
              i = adjacent_layout->view.rect.left;
         }else{
              break;
         }
    }

    for(int64_t i = layout->view.rect.right + 1; i <= rect.right; i++){
         CePoint_t point = {i, layout->view.rect.bottom};
         CeLayout_t* adjacent_layout = ce_layout_find_at(root, point);
         if(adjacent_layout && adjacent_layout->view.rect.bottom == layout->view.rect.bottom){
              adjacent_layout->view.rect.bottom = result->new_value;
              i = adjacent_layout->view.rect.right;
         }else{
              break;
         }
    }
}

bool ce_layout_resize_rect(CeLayout_t* root, CeLayout_t* layout, CeRect_t rect, CeDirection_t direction, bool expand, int64_t amount){
     switch(layout->type){
     default:
          break;
     case CE_LAYOUT_TYPE_VIEW:
     {
          ResizeRectResult_t result = resize_rect(&layout->view.rect, rect, direction, expand, amount);

          if(result.new_value < 0) return false;

          switch(direction){
          default:
               break;
          case CE_LEFT:
               if(expand){
                    resize_opposites_left(root, layout, rect, &result);
                    resize_shareds_left(root, layout, rect, &result);
               }else{
                    resize_shareds_left(root, layout, rect, &result);
                    resize_opposites_left(root, layout, rect, &result);
               }

               layout->view.rect.left = result.new_value;
               break;
          case CE_RIGHT:
               if(expand){
                    resize_opposites_right(root, layout, rect, &result);
                    resize_shareds_right(root, layout, rect, &result);
               }else{
                    resize_shareds_right(root, layout, rect, &result);
                    resize_opposites_right(root, layout, rect, &result);
               }

               layout->view.rect.right = result.new_value;
               break;
          case CE_UP:
               if(expand){
                    resize_opposites_up(root, layout, rect, &result);
                    resize_shareds_up(root, layout, rect, &result);
               }else{
                    resize_shareds_up(root, layout, rect, &result);
                    resize_opposites_up(root, layout, rect, &result);
               }

               layout->view.rect.top = result.new_value;
               break;
          case CE_DOWN:
               if(expand){
                    resize_opposites_down(root, layout, rect, &result);
                    resize_shareds_down(root, layout, rect, &result);
               }else{
                    resize_shareds_down(root, layout, rect, &result);
                    resize_opposites_down(root, layout, rect, &result);
               }

               layout->view.rect.bottom = result.new_value;
               break;
          }
     } break;
     case CE_LAYOUT_TYPE_LIST:
          break;
     case CE_LAYOUT_TYPE_TAB:
          break;
     case CE_LAYOUT_TYPE_TAB_LIST:
          break;
     }

     return true;
}

CeLayout_t* ce_layout_find_at(CeLayout_t* layout, CePoint_t point){
     switch(layout->type){
     default:
          break;
     case CE_LAYOUT_TYPE_VIEW:
          if(ce_point_in_rect(point, layout->view.rect)) return layout;
          break;
     case CE_LAYOUT_TYPE_LIST:
          for(int64_t i = 0; i < layout->list.layout_count; i++){
               CeLayout_t* found = ce_layout_find_at(layout->list.layouts[i], point);
               if(found) return found;
          }
          break;
     case CE_LAYOUT_TYPE_TAB:
          return ce_layout_find_at(layout->tab.root, point);
     case CE_LAYOUT_TYPE_TAB_LIST:
          return ce_layout_find_at(layout->tab_list.current, point);
     }

     return NULL;
}

CeLayout_t* ce_layout_find_parent(CeLayout_t* root, CeLayout_t* node){
     switch(root->type){
     default:
          break;
     case CE_LAYOUT_TYPE_VIEW:
          break;
     case CE_LAYOUT_TYPE_LIST:
          for(int64_t i = 0; i < root->list.layout_count; i++){
               if(root->list.layouts[i] == node) return root;
               CeLayout_t* found = ce_layout_find_parent(root->list.layouts[i], node);
               if(found) return found;
          }
          break;
     case CE_LAYOUT_TYPE_TAB:
          if(root->tab.root == node) return root;
          else return ce_layout_find_parent(root->tab.root, node);
          break;
     case CE_LAYOUT_TYPE_TAB_LIST:
          for(int64_t i = 0; i < root->tab_list.tab_count; i++){
               if(root->tab_list.tabs[i] == node) return root;
               CeLayout_t* found = ce_layout_find_parent(root->tab_list.tabs[i], node);
               if(found) return found;
          }
          break;
     }

     return NULL;
}

bool ce_layout_delete(CeLayout_t* root, CeLayout_t* node){
     CeLayout_t* parent = ce_layout_find_parent(root, node);
     if(!parent) return false;

     switch(parent->type){
     default:
          return false;
     case CE_LAYOUT_TYPE_LIST:
     {
          CeRect_t parent_rect = ce_layout_rect(parent);

          // find index of matching node
          int64_t index = -1;
          for(int64_t i = 0; i < parent->list.layout_count; i++){
               if(parent->list.layouts[i] == node){
                    index = i;
                    break;
               }
          }

          if(index == -1) return false;

          ce_layout_free(&node);

          // remove element, keeping element order
          int64_t new_count = parent->list.layout_count - 1;
          for(int64_t i = index; i < new_count; i++){
               parent->list.layouts[i] = parent->list.layouts[i + 1];
          }
          parent->list.layout_count = new_count;
          ce_layout_distribute_rect(parent, parent_rect);
          assert(!(parent->list.layout_count == 0 && parent == root));

          if(new_count == 0){
              return ce_layout_delete(root, parent);
          }else if(new_count == 1){
               // reduce back to a single layout
               CeLayout_t* grandparent = ce_layout_find_parent(root, parent);
               if(!grandparent) return true;

               switch(grandparent->type){
               default:
                    return false;
               case CE_LAYOUT_TYPE_LIST:
                    // replace the parent at that element
                    for(int64_t i = 0; i < grandparent->list.layout_count; i++){
                         if(grandparent->list.layouts[i] == parent){
                              grandparent->list.layouts[i] = parent->list.layouts[0];
                              parent->list.layouts[0] = NULL;
                              break;
                         }
                    }
                    break;
               case CE_LAYOUT_TYPE_TAB:
                    grandparent->tab.root = parent->list.layouts[0];
                    parent->list.layouts[0] = NULL;
                    break;
               case CE_LAYOUT_TYPE_TAB_LIST:
                    return false;
               }

               ce_layout_free(&parent);
          }
     } break;
     case CE_LAYOUT_TYPE_TAB:
          break;
     case CE_LAYOUT_TYPE_TAB_LIST:
     {
          // find index of matching node
          int64_t index = -1;
          for(int64_t i = 0; i < parent->tab_list.tab_count; i++){
               if(parent->tab_list.tabs[i] == node){
                    index = i;
                    break;
               }
          }

          if(index == -1) return false;

          ce_layout_free(&node);

          // remove element, keeping element order
          int64_t new_count = parent->tab_list.tab_count - 1;
          for(int64_t i = index; i < new_count; i++){
               parent->tab_list.tabs[i] = parent->tab_list.tabs[i + 1];
          }
          parent->tab_list.tab_count = new_count;
     } break;
     }

     return true;
}

CeLayout_t* ce_layout_buffer_in_view(CeLayout_t* layout, CeBuffer_t* buffer){
     switch(layout->type){
     default:
          break;
     case CE_LAYOUT_TYPE_VIEW:
          if(layout->view.buffer == buffer) return layout;
          break;
     case CE_LAYOUT_TYPE_LIST:
          for(int64_t i = 0; i < layout->list.layout_count; i++){
               CeLayout_t* found = ce_layout_buffer_in_view(layout->list.layouts[i], buffer);
               if(found) return found;
          }
          break;
     case CE_LAYOUT_TYPE_TAB:
          return ce_layout_buffer_in_view(layout->tab.root, buffer);
     case CE_LAYOUT_TYPE_TAB_LIST:
          return ce_layout_buffer_in_view(layout->tab_list.current, buffer);
     }

     return NULL;
}

int64_t count_buffer_in_views(CeLayout_t* layout, CeBuffer_t* buffer, int64_t count){
     switch(layout->type){
     default:
          break;
     case CE_LAYOUT_TYPE_VIEW:
          if(layout->view.buffer == buffer) return 1;
          break;
     case CE_LAYOUT_TYPE_LIST:
          for(int64_t i = 0; i < layout->list.layout_count; i++){
               count = count_buffer_in_views(layout->list.layouts[i], buffer, count);
          }
          break;
     case CE_LAYOUT_TYPE_TAB:
          return count_buffer_in_views(layout->tab.root, buffer, count);
     case CE_LAYOUT_TYPE_TAB_LIST:
          return count_buffer_in_views(layout->tab_list.current, buffer, count);
     }

     return count;
}

void build_buffer_in_views(CeLayout_t* layout, CeBuffer_t* buffer, CeLayoutBufferInViewsResult_t* result){
     switch(layout->type){
     default:
          break;
     case CE_LAYOUT_TYPE_VIEW:
          if(layout->view.buffer == buffer){
               result->layouts[result->layout_count] = layout;
               result->layout_count++;
          }
          break;
     case CE_LAYOUT_TYPE_LIST:
          for(int64_t i = 0; i < layout->list.layout_count; i++){
               build_buffer_in_views(layout->list.layouts[i], buffer, result);
          }
          break;
     case CE_LAYOUT_TYPE_TAB:
          build_buffer_in_views(layout->tab.root, buffer, result);
          break;
     case CE_LAYOUT_TYPE_TAB_LIST:
          build_buffer_in_views(layout->tab_list.current, buffer, result);
          break;
     }
}

CeLayoutBufferInViewsResult_t ce_layout_buffer_in_views(CeLayout_t* layout, CeBuffer_t* buffer){
     CeLayoutBufferInViewsResult_t result = {};
     int64_t count = count_buffer_in_views(layout, buffer, 0);
     result.layouts = malloc(count * sizeof(*result.layouts));
     build_buffer_in_views(layout, buffer, &result);
     return result;
}

static int64_t count_children(CeLayout_t* layout){
     switch(layout->type){
     default:
          break;
     case CE_LAYOUT_TYPE_VIEW:
          return 1;
     case CE_LAYOUT_TYPE_LIST:
     {
          int sum = 0;
          for(int64_t i = 0; i < layout->list.layout_count; i++){
               sum += count_children(layout->list.layouts[i]);
          }
          return sum;
     }
     }

     return 0;
}

int64_t ce_layout_tab_get_layout_count(CeLayout_t* layout){
     switch(layout->type){
     default:
          break;
     case CE_LAYOUT_TYPE_TAB:
          if(!layout->tab.root) return 0;
          return count_children(layout->tab.root);
     }

     return 0;
}
