#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/engine.hpp>
#include <Godot/classes/scene_tree.hpp>
#include <Godot/classes/window.hpp>

using namespace godot;
using namespace jenova::sdk;

// Считаем слова
static int64_t count_words_safe(const String& str) {
	String s = str.strip_edges();
	if (s.is_empty()) return 0;
	
	int64_t words = 0;
	bool in_word = false;
	
	for (int i = 0; i < s.length(); i++) {
		char32_t c = s[i];
		if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
			in_word = false;
		} else {
			if (!in_word) {
				words++;
				in_word = true;
			}
		}
	}
	return words;
}

// Рекурсивный поиск стен
static int64_t count_textwall_words(Node* node) {
	if (!node) return 0;
	int64_t total_words = 0;

	// Ищем свойство block_text (Duck Typing)
	Variant block_text_var = node->get("block_text");
	if (block_text_var.get_type() == Variant::STRING) {
		
		String text = block_text_var;
		int64_t words_per_tile = count_words_safe(text);

		if (words_per_tile > 0) {
			Node* chunks_root = node->get_node_or_null("ChunksRoot");
			if (chunks_root) {
				int64_t total_quads = 0;
				int child_count = chunks_root->get_child_count();
				
				for (int i = 0; i < child_count; i++) {
					Node* mmi = chunks_root->get_child(i);
					if (mmi) {
						Variant mm_var = mmi->get("multimesh");
						if (mm_var.get_type() == Variant::OBJECT) {
							Object* mm_obj = mm_var;
							if (mm_obj) {
								// Используем int64_t для безопасного каста из Variant
								int64_t instances = mm_obj->call("get_instance_count");
								total_quads += instances;
							}
						}
					}
				}
				total_words += words_per_tile * total_quads;
			}
		}
	}

	// Идем вглубь по детям
	int child_count = node->get_child_count();
	for (int i = 0; i < child_count; i++) {
		Node* child = node->get_child(i);
		if (child) {
			total_words += count_textwall_words(child);
		}
	}

	return total_words;
}

static float scan_timer = 1.0f; 
static int64_t cached_words_count = 0; 

JENOVA_SCRIPT_BEGIN

// Добавим OnAwake, чтобы видеть, что скрипт вообще запустился при старте игры
void OnAwake(Caller* instance) {
}

void OnProcess(Caller* instance, double delta) {
	Node* self_node = GetSelf<Node>(instance);
	if (!self_node) return;

	SceneTree* tree = self_node->get_tree();
	if (!tree) return; 

	double fps = Engine::get_singleton()->get_frames_per_second();

	scan_timer += (float)delta;
	if (scan_timer >= 1.0f) {
		scan_timer = 0.0f;
		
		Node* root = tree->get_root();
		if (root) {
			cached_words_count = count_textwall_words(root);
			
			// === ВЫВОД В КОНСОЛЬ GODOT ===
			// Если на экране ничего нет, вы увидите это в панели Output в редакторе!
			Output("[WordsCounter] FPS: %.1f | Words found: %lld", fps, cached_words_count);
		}
	}

	String output = String("FPS: ") + String::num(fps) + String("\n");
	output += String("Words: ") + String::num_int64(cached_words_count);

	// Устанавливаем текст (если нода - Label)
	self_node->call("set_text", output);
}

JENOVA_SCRIPT_END