#ifndef ns_animal_telemetry_h
#define ns_animal_telemetry_h
#include "ns_time_path_image_analyzer.h"
#include "ns_graph.h"


struct ns_animal_list_at_position {
	ns_stationary_path_id stationary_path_id;
	typedef std::vector<ns_death_timing_data> ns_animal_list;  //positions can hold multiple animals
	ns_animal_list animals;
};

class ns_death_time_posture_solo_annotater_region_data {
public:
 ns_death_time_posture_solo_annotater_region_data() :movement_data_loaded(false), annotation_file("","", ""), loading_time(0), loaded_path_data_successfully(false), movement_quantification_data_loaded(false){}
	
	void load_movement_analysis(const ns_64_bit region_id_, ns_sql & sql, const bool load_movement_quantification = false) {
		if (!movement_data_loaded) {
			solution.load_from_db(region_id_, sql, true);
		}
		if (movement_quantification_data_loaded)
			return;

		if (image_server.verbose_debug_output()) image_server.register_server_event_no_db(ns_image_server_event("Loading time series denoising parameters"));
		const ns_time_series_denoising_parameters time_series_denoising_parameters(ns_time_series_denoising_parameters::load_from_db(region_id_, sql));
		ns_image_server::ns_posture_analysis_model_cache::const_handle_t handle;
		image_server.get_posture_analysis_model_for_region(region_id_, handle, sql);
		ns_acquire_for_scope<ns_analyzed_image_time_path_death_time_estimator> death_time_estimator(
			ns_get_death_time_estimator_from_posture_analysis_model(handle().model_specification
			));
		handle.release();

		if (image_server.verbose_debug_output()) image_server.register_server_event_no_db(ns_image_server_event("Loading completed analysis"));
		loaded_path_data_successfully = movement_analyzer.load_completed_analysis(region_id_, solution, time_series_denoising_parameters, &death_time_estimator(), sql, !load_movement_quantification);
		death_time_estimator.release();
		movement_data_loaded = true;
		if (load_movement_quantification)
			movement_quantification_data_loaded = true;
	}
	void load_images_for_worm(const ns_stationary_path_id & path_id, const unsigned long number_of_images_to_load, ns_sql & sql) {
		const unsigned long current_time(ns_current_time());
		const unsigned long max_number_of_cached_worms(2);
		//don't allow more than 2 paths to be loaded (to prevent memory overflow). Delete animals that have been in the cache for longest.
		if (image_loading_times_for_groups.size() >= max_number_of_cached_worms) {

			//delete animals that are older than 5 minutes.
			const unsigned long cutoff_time(current_time - 5 * 60);
			for (ns_loading_time_cache::iterator p = image_loading_times_for_groups.begin(); p != image_loading_times_for_groups.end(); ) {
				if (p->second < cutoff_time) {
					movement_analyzer.clear_images_for_group(p->first);
					image_loading_times_for_groups.erase(p++);
				}
				else p++;
			}
			//delete younger ones if necissary
			while (image_loading_times_for_groups.size() >= max_number_of_cached_worms) {
				ns_loading_time_cache::iterator youngest(image_loading_times_for_groups.begin());
				for (ns_loading_time_cache::iterator p = image_loading_times_for_groups.begin(); p != image_loading_times_for_groups.end(); p++) {
					if (youngest->second > p->second)
						youngest = p;
				}
				movement_analyzer.clear_images_for_group(youngest->first);
				image_loading_times_for_groups.erase(youngest);
			}
		}
		movement_analyzer.load_images_for_group(path_id.group_id, number_of_images_to_load, sql, false, false);
		image_loading_times_for_groups[path_id.group_id] = current_time;
	}
	void clear_images_for_worm(const ns_stationary_path_id & path_id) {
		movement_analyzer.clear_images_for_group(path_id.group_id);
		ns_loading_time_cache::iterator p = image_loading_times_for_groups.find(path_id.group_id);
		if (p != image_loading_times_for_groups.end())
			image_loading_times_for_groups.erase(p);
	}

	ns_time_path_image_movement_analyzer movement_analyzer;

	std::vector<ns_animal_list_at_position> by_hand_timing_data; //organized by group.  that is movement_analyzer.group(4) is timing_data(4)
	std::vector<ns_animal_list_at_position> machine_timing_data; //organized by group.  that is movement_analyzer.group(4) is timing_data(4)
	std::vector<ns_death_time_annotation> orphaned_events;
	ns_region_metadata metadata;

	//group id,time of loading
	typedef std::map<long, unsigned long> ns_loading_time_cache;
	ns_loading_time_cache image_loading_times_for_groups;

	mutable ns_image_server_results_file annotation_file;
	unsigned long loading_time;
	bool load_annotations(ns_sql & sql, const bool load_by_hand = true) {
		orphaned_events.resize(0);
		ns_timing_data_and_death_time_annotation_matcher <std::vector<ns_animal_list_at_position> > matcher;
		bool could_load_by_hand(false);
		if (load_by_hand) {
			//load by hand annotations
			ns_acquire_for_scope<std::istream> in(annotation_file.input());
			if (!in.is_null()) {
				ns_death_time_annotation_set set;
				set.read(ns_death_time_annotation_set::ns_all_annotations, in());

				std::string error_message;
				if (matcher.load_timing_data_from_set(set, false, by_hand_timing_data, orphaned_events, error_message)) {
					if (error_message.size() != 0) {
						ns_update_information_bar(error_message);
						could_load_by_hand = true;
					}

				}
			}
		}

		//load machine annotations
		ns_machine_analysis_data_loader machine_annotations;
		machine_annotations.load(ns_death_time_annotation_set::ns_censoring_and_movement_transitions, metadata.region_id, 0, 0, sql, true);
		if (machine_annotations.samples.size() != 0 && machine_annotations.samples.begin()->regions.size() != 0) {
			std::vector<ns_death_time_annotation> _unused_orphaned_events;
			std::string _unused_error_message;
			matcher.load_timing_data_from_set(machine_annotations.samples.begin()->regions.begin()->death_time_annotation_set, true,
				machine_timing_data, _unused_orphaned_events, _unused_error_message);

			return true;
		}



		return could_load_by_hand;
	}
	void add_annotations_to_set(ns_death_time_annotation_set & set, std::vector<ns_death_time_annotation> & orphaned_events) {
		ns_death_time_annotation_set t_set;
		std::vector<ns_death_time_annotation> t_orphaned_events;
		ns_timing_data_and_death_time_annotation_matcher<std::vector<ns_death_timing_data> > matcher;
		for (unsigned int i = 0; i < by_hand_timing_data.size(); i++)
			matcher.save_death_timing_data_to_set(by_hand_timing_data[i].animals, t_orphaned_events, t_set, false);
		orphaned_events.insert(orphaned_events.end(), t_orphaned_events.begin(), t_orphaned_events.end());
		set.add(t_set);
	}

	void save_annotations(const ns_death_time_annotation_set & extra_annotations) const {
		throw ns_ex("The solo posture annotations should be mixed in with storyboard annotations!");
		ns_death_time_annotation_set set;


		ns_acquire_for_scope<std::ostream> out(annotation_file.output());
		if (out.is_null())
			throw ns_ex("Could not open output file.");
		set.write(out());
		out.release();
		//	ns_update_information_bar(ns_to_string(set.events.size()) + " events saved at " + ns_format_time_string_for_human(ns_current_time()));
		//	saved_=true;
	};
	bool movement_quantification_data_is_loaded() { return movement_quantification_data_loaded; }
	bool loaded_path_data_successfully;
private:
	ns_time_path_solution solution;
	bool movement_data_loaded,
		movement_quantification_data_loaded;
};

class ns_animal_telemetry {
public:
	typedef enum { ns_none, ns_movement, ns_movement_intensity, ns_movement_intensity_slope, ns_all,ns_number_of_graph_types } ns_graph_contents;
private:
	bool _show;
	ns_death_time_posture_solo_annotater_region_data *region_data;
	unsigned long group_id;
	ns_image_standard base_graph;
	ns_graph graph;
	ns_graph_specifics graph_specifics;
	vector<ns_graph_object> movement_vals, smoothed_movement_vals, size_vals, slope_vals;
	vector <double> time_axis;
	vector<long> segment_ids; //mapping from the current time in time_axis to the specicif segment (element of movement_vals, smoothed_movement_vals, etc)
	vector<long> segment_offsets; //mapping from the current time in time_axis to the specicif segment (element of movement_vals, smoothed_movement_vals, etc)
	ns_posture_analysis_model posture_analysis_model;
	
	void draw_base_graph(const ns_graph_contents & graph_contents) {
		if (graph_contents == ns_none)
			return;
		graph.clear();
		ns_analyzed_image_time_path *path(&region_data->movement_analyzer.group(group_id).paths[0]);
		if (path->element_count() < 1)
			throw ns_ex("Time series is too short");

		time_axis.resize(path->element_count());
		segment_ids.resize(path->element_count(), -1);

		//calculate the average time step duration, properly handling excluded time steps
		ns_64_bit time_step(0);
		unsigned long time_step_count(0);
		unsigned long last_valid_time(0);
		for (unsigned int i = 0; i < path->element_count(); i++) {
			if (!path->element(i).excluded) {
				if (last_valid_time > 0) {
					time_step += (path->element(i).absolute_time - last_valid_time);
					time_step_count++;
				}
				last_valid_time = path->element(i).absolute_time;
			}
		}

		const unsigned long max_time_step_interval_to_connect_with_lines(4*time_step / time_step_count);

		//count how many connected line segments we'll need to draw.
		long number_of_separate_segments(1);
		last_valid_time = 0;
		for (unsigned int i = 0; i < path->element_count(); i++) {
			if (path->element(i).excluded)
				continue;
			unsigned long current_time_step = (path->element(i).absolute_time - last_valid_time);
			bool step_was_too_long(last_valid_time > 0 && current_time_step > max_time_step_interval_to_connect_with_lines);
			if (step_was_too_long)
				number_of_separate_segments++;
			segment_ids[i] = number_of_separate_segments-1;
			last_valid_time = path->element(i).absolute_time;
		}
		segment_offsets.resize(number_of_separate_segments);
		if (segment_offsets.size()>0)
			segment_offsets[0] = 0;
		unsigned long cur_seg_id(0);
		for (unsigned int i = 0; i < path->element_count(); i++) {
			if (segment_ids[i] != cur_seg_id) {
				cur_seg_id++;
				segment_offsets[cur_seg_id] = i;
			}
		}

		movement_vals.resize(number_of_separate_segments, ns_graph_object::ns_graph_dependant_variable);
		slope_vals.resize(number_of_separate_segments, ns_graph_object::ns_graph_dependant_variable);
		size_vals.resize(number_of_separate_segments, ns_graph_object::ns_graph_dependant_variable);
		for (unsigned int i = 0; i < number_of_separate_segments; i++) {
			movement_vals[i].x.resize(0);
			movement_vals[i].x.reserve(path->element_count());
			movement_vals[i].y.resize(0);
			movement_vals[i].y.reserve(path->element_count());

			slope_vals[i].x.resize(0);
			slope_vals[i].x.reserve(path->element_count());
			slope_vals[i].y.resize(0);
			slope_vals[i].y.reserve(path->element_count());
			size_vals[i].x.resize(0);
			size_vals[i].x.reserve(path->element_count());
			size_vals[i].y.resize(0);
			size_vals[i].y.reserve(path->element_count());
		}

		float min_score(FLT_MAX), max_score(-FLT_MAX);
		float min_intensity(FLT_MAX), max_intensity(-FLT_MAX);
		float min_intensity_slope(0),  //we want to include a zero slope
			max_intensity_slope(-FLT_MAX);

		float min_raw_score, max_raw_score, second_min_raw_score;
		float min_time(FLT_MAX), max_time(-FLT_MAX);
		
		std::vector<double> scores;
		scores.reserve(path->element_count());

		//find lowest movement score.
		for (unsigned int i = 0; i < path->element_count(); i++) {
			if (path->element(i).excluded)
				continue;
			scores.push_back(path->element(i).measurements.death_time_posture_analysis_measure_v2());
		}
		std::sort(scores.begin(), scores.end());
		min_raw_score = scores[scores.size() / 50];
		second_min_raw_score = min_raw_score *= 1.01; //default
		for (int i = scores.size() / 50; i < scores.size(); i++) {
			if (scores[i] > min_raw_score) {
				second_min_raw_score = scores[i];
				break;
			}
		}
			max_raw_score = scores[scores.size() - scores.size() / 50 - 1];
		if (max_raw_score == min_raw_score) {
			min_raw_score -= .01;
			max_raw_score += .01;
		}

		//now reuse scores memory for another purpose--storing normalized movement scores.
		scores.resize(path->element_count());
		//calculate normalized movement scores and find their min and max
		for (unsigned int i = 0; i < path->element_count(); i++) {
			if (path->element(i).excluded)
				continue;
			double d(path->element(i).measurements.death_time_posture_analysis_measure_v2());
			if (d >= max_raw_score) d = log(max_raw_score - min_raw_score);
			else if (d <= min_raw_score)
				d = log(second_min_raw_score - min_raw_score); //can't be zero before we take the log
			else d = log(d - min_raw_score);

			scores[i] = d;

			if (d < min_score) min_score = d;
			if (d > max_score) max_score = d;
		}

		//find min max of other statistics
		for (unsigned int i = 0; i < path->element_count(); i++) {
			if (path->element(i).excluded)
				continue;
			const double t(path->element(i).relative_time);
			if (t < min_time) min_time = t;
			if (t > max_time) max_time = t;
			if (i < path->first_stationary_timepoint())
				continue;
			const double n(path->element(i).measurements.total_intensity_within_stabilized);
			const double s(path->element(i).measurements.change_in_total_stabilized_intensity);
			if (n < min_intensity) min_intensity = n;
			if (n > max_intensity) max_intensity = n;
			if (s < min_intensity_slope) min_intensity_slope = s;
			if (s > max_intensity_slope) max_intensity_slope = s;
		}

		double threshold;
		if (posture_analysis_model.threshold_parameters.stationary_cutoff >= max_raw_score)
			threshold = max_raw_score - min_raw_score;
		else if (posture_analysis_model.threshold_parameters.stationary_cutoff <= min_raw_score)
			threshold = second_min_raw_score - min_raw_score;
		else threshold = posture_analysis_model.threshold_parameters.stationary_cutoff - min_raw_score;
		threshold = log(threshold);


		double min_rounded_time(DBL_MAX), max_rounded_time(DBL_MIN);
		//calculate normalized scores and break up into independantly plotted segments
		for (unsigned int i = 0; i < path->element_count(); i++) {
			const long & current_segment = segment_ids[i];
			const double time = floor(path->element(i).relative_time / 6.0 / 60 / 24) / 10;
			if (time < min_rounded_time) min_rounded_time = time;
			if (time > max_rounded_time) max_rounded_time = time;
			time_axis[i] = time;
			if (path->element(i).excluded || segment_ids[i] == -1)
				continue;
			//scale denoised movement score to a value between 0 and 1
			//which on log scale becomes 0 and 1

			movement_vals[current_segment].x.push_back(time);
			movement_vals[current_segment].y.push_back((scores[i] - min_score) / (max_score - min_score));
			if (*movement_vals[current_segment].y.rbegin() < 0) *movement_vals[current_segment].y.rbegin() = 0;
			if (*movement_vals[current_segment].y.rbegin() > 1) *movement_vals[current_segment].y.rbegin() = 1;

			//scale intensity to a value between 0 and 1.
			size_vals[current_segment].x.push_back(time);
			size_vals[current_segment].y.push_back((path->element(i).measurements.total_intensity_within_stabilized - min_intensity) / (max_intensity-min_intensity)) ;
			if (*size_vals[current_segment].y.rbegin() < 0) *size_vals[current_segment].y.rbegin() = 0;
			if (*size_vals[current_segment].y.rbegin() > 1) *size_vals[current_segment].y.rbegin() = 1;


			slope_vals[current_segment].x.push_back(time);
			slope_vals[current_segment].y.push_back((path->element(i).measurements.change_in_total_stabilized_intensity - min_intensity_slope) / (max_intensity_slope - min_intensity_slope));
			if (*slope_vals[current_segment].y.rbegin() < 0) *slope_vals[current_segment].y.rbegin() = 0;
			if (*slope_vals[current_segment].y.rbegin() > 1) *slope_vals[current_segment].y.rbegin() = 1;
		}

		ns_graph_object threshold_object(ns_graph_object::ns_graph_horizontal_line);
		double th((threshold- min_score)/(max_score - min_score));
		if (th< 0) th = 0;
		if (th > 1) th = 1;
		threshold_object.y.push_back(th);


		double zero_slope_value = (0 - min_intensity_slope) / (max_intensity_slope - min_intensity_slope);

		ns_graph_object zero_slope_object(ns_graph_object::ns_graph_horizontal_line);
		zero_slope_object.y.push_back(zero_slope_value);



		smoothed_movement_vals.resize(movement_vals.size(), ns_graph_object::ns_graph_dependant_variable);
		for (unsigned int j = 0; j < movement_vals.size(); j++) {
			smoothed_movement_vals[j].x.resize(0);
			smoothed_movement_vals[j].x.insert(smoothed_movement_vals[j].x.end(), movement_vals[j].x.begin(), movement_vals[j].x.end());
			smoothed_movement_vals[j].y.resize(movement_vals[j].y.size());
			for (int i = 0; i < movement_vals[j].y.size(); i++) {
				int di = i - 4;
				int ddi = i + 4;
				if (di < 0) di = 0;
				if (ddi >= movement_vals[j].y.size()) ddi = movement_vals[j].y.size() - 1;
				float sum(0);
				for (int k = di; k <= ddi; k++)
					sum += movement_vals[j].y[k];
				smoothed_movement_vals[j].y[i] = sum / (ddi - di + 1);
			}
		}

		threshold_object.properties.line.color = ns_color_8(150, 150, 150);
		threshold_object.properties.line.draw = true;

		zero_slope_object.properties.line.color = ns_color_8(150, 150, 150);
		zero_slope_object.properties.line.draw = true;
		for (unsigned int i = 0; i < number_of_separate_segments; i++) {
			//time_axes[i].x_label = "age (days)";
			//time_axes[i].properties.text.color = ns_color_8(255, 255, 255);
			//time_axes[i].properties.line.color = ns_color_8(255, 255, 255);
			smoothed_movement_vals[i].y_label = "Movement score";
			smoothed_movement_vals[i].properties.text.color = ns_color_8(255, 255, 255);
			smoothed_movement_vals[i].properties.line.color = ns_color_8(255, 255, 255);
			size_vals[i].properties.line.color = ns_color_8(125, 125, 255);
			smoothed_movement_vals[i].properties.line.draw = size_vals[i].properties.line.draw = true;
			smoothed_movement_vals[i].properties.line.width = size_vals[i].properties.line.width = 1;
			smoothed_movement_vals[i].properties.point.draw = size_vals[i].properties.point.draw = false;

			movement_vals[i].properties.line.draw = false;
			movement_vals[i].properties.point.draw = true;
			movement_vals[i].properties.point.width = 1;
			movement_vals[i].properties.point.color = ns_color_8(200, 200, 200);
			movement_vals[i].properties.point.edge_width = 1;

			slope_vals[i].properties = smoothed_movement_vals[i].properties;
			slope_vals[i].properties.line.color = ns_color_8(150, 250, 200);

			graph.contents.push_back(movement_vals[i]);
			if (graph_contents == ns_movement_intensity_slope || graph_contents == ns_all) {
				graph.contents.push_back(slope_vals[i]);
			}
			graph.contents.push_back(smoothed_movement_vals[i]);
			if (graph_contents == ns_movement_intensity || graph_contents == ns_all)
				graph.contents.push_back(size_vals[i]);
			//graph.contents.push_back(time_axes);
		}
		if (graph_contents == ns_movement_intensity_slope || graph_contents == ns_all) 
			graph.contents.push_back(zero_slope_object);
		graph.contents.push_back(threshold_object);


		


		graph.x_axis_properties.line.color = graph.y_axis_properties.line.color = ns_color_8(255, 255, 255);
		graph.x_axis_properties.text.color = graph.y_axis_properties.text.color = ns_color_8(255, 255, 255);
		graph.x_axis_properties.text_size = graph.y_axis_properties.text_size = 10;
		graph.area_properties.area_fill.color = ns_color_8(0, 0, 0);

		ns_graph_axes axes;
		axes.boundary(0) = min_rounded_time;
		axes.boundary(1) = max_rounded_time;
		axes.boundary(2) = 0;
		axes.boundary(3) = 1;
		ns_color_8 gray(50, 50, 50);
		graph.x_axis_properties.line.width = 
			graph.y_axis_properties.line.width = 1;
		graph.x_axis_properties.line.color =
			graph.y_axis_properties.line.color = gray;

		graph.set_graph_display_options("", axes, base_graph.properties().width/(float)base_graph.properties().height);
		graph_specifics = graph.draw(base_graph);
	}
	inline void map_value_from_graph_onto_image(const float &x, const float &y, unsigned long & x1, unsigned long & y1) {
		x1 = graph_specifics.boundary.x + (unsigned int)(graph_specifics.dx*(x - graph_specifics.axes.boundary(0) + graph_specifics.axes.axis_offset(0)));

		y1 = base_graph.properties().height - graph_specifics.boundary.y - (unsigned int)(graph_specifics.dy*(y - graph_specifics.axes.boundary(2) + graph_specifics.axes.axis_offset(1)));
	}
	inline unsigned long map_pixel_from_image_onto_buffer(const unsigned long &x, const unsigned long &y, const ns_vector_2i &position, const ns_vector_2i &buffer_size) {
		return 3 * ((buffer_size.y - y - position.y-1)*buffer_size.x + x + position.x);
	}
	void overlay_metadata(const ns_animal_telemetry::ns_graph_contents graph_contents,const unsigned long current_element, const ns_vector_2i & position, const ns_vector_2i & buffer_size, ns_8_bit * buffer) {
		unsigned long x_score, y_score, x_size, y_size;
		long segment_id = segment_ids[current_element];
		if (segment_id == -1)
			return;
		if (segment_id > movement_vals.size())
			throw ns_ex("Invalid segment id!");
		long segment_element_id = current_element - segment_offsets[segment_id];
		if (segment_element_id >= movement_vals[segment_id].y.size())
			throw ns_ex("Out of element id");
		map_value_from_graph_onto_image(time_axis[current_element], movement_vals[segment_id].y[segment_element_id], x_score, y_score);
		map_value_from_graph_onto_image(time_axis[current_element], size_vals[segment_id].y[segment_element_id], x_size, y_size);
		for (int y = -2; y <= 2; y++)
			for (int x = -2; x <= 2; x++) {
				unsigned long p(map_pixel_from_image_onto_buffer(x_score+x+border().x, y_score+y+ border().y, position, buffer_size));
				buffer[p] = 255;
				buffer[p+1] = 0;
				buffer[p+2] = 0;
				if (graph_contents == ns_animal_telemetry::ns_all || graph_contents == ns_animal_telemetry::ns_movement_intensity) {
					p = map_pixel_from_image_onto_buffer(x_size + x + border().x, y_size + y + border().y, position, buffer_size);
					buffer[p] = 255;
					buffer[p + 1] = 0;
					buffer[p + 2] = 0;
				}
			}

	}
	ns_graph_contents last_graph_contents;
public:
	void clear() {
		_show = false;
		region_data = 0;
		group_id = 0;
		base_graph.clear();
		graph.clear();
		last_graph_contents = ns_none;
		movement_vals.clear();
		smoothed_movement_vals.clear();
		size_vals.clear();
		slope_vals.clear();
		time_axis.clear();
		ns_posture_analysis_model posture_analysis_model;
	}
	unsigned long get_graph_time_from_graph_position(const float x) { //x is in relative time
		ns_analyzed_image_time_path *path(&region_data->movement_analyzer.group(group_id).paths[0]);

		unsigned long dT(path->element(path->element_count()-1).absolute_time - path->element(0).absolute_time);
		float dt((path->element(path->element_count()-1).relative_time - path->element(0).relative_time) / 60.0 / 60.0 / 24.0);
		return ((x - path->element(0).relative_time / 60.0 / 60.0 / 24.0) / dt)*dT + path->element(0).absolute_time;

	}
	ns_vector_2i get_graph_value_from_click_position(const unsigned long &x, const unsigned long & y) const{
		ns_vector_2i res;
		res.x = ((long)x - (long)graph_specifics.boundary.x - (long)border().x) / (graph_specifics.dx) + graph_specifics.axes.boundary(0) - graph_specifics.axes.axis_offset(0);

		//y = base_graph.properties().height - graph_specifics.boundary.y - (unsigned int)(graph_specifics.dy*(y - graph_specifics.axes.boundary(2) + graph_specifics.axes.axis_offset(1)));
		res.y = -((long)y - base_graph.properties().height + graph_specifics.boundary.y + border().y) / graph_specifics.dy + graph_specifics.axes.boundary(2) - graph_specifics.axes.axis_offset(1);
		return res;
	}
	ns_vector_2i image_size() const { 
		if (!_show) return ns_vector_2i(0, 0);
		return largest_possible_image_size();
	}	
	ns_vector_2i largest_possible_image_size() const {
		return ns_vector_2i(300, 300);
	}
	ns_vector_2i border() const {
		return ns_vector_2i(25, 25);
	}
	ns_animal_telemetry() :_show(false), last_graph_contents(ns_none), region_data(0), group_id(0){}
	void set_current_animal(const unsigned int & group_id_, ns_posture_analysis_model & mod,ns_death_time_posture_solo_annotater_region_data * region_data_) {
		group_id = group_id_;
		region_data = region_data_;
		base_graph.init(ns_image_properties(0, 0, 3));
		posture_analysis_model = mod;
	}
	void draw(const ns_graph_contents graph_contents, const unsigned long element_id, const ns_vector_2i & position, const ns_vector_2i & graph_size, const ns_vector_2i & buffer_size, ns_8_bit * buffer) {
		if (base_graph.properties().height == 0 || graph_contents != last_graph_contents) {
			base_graph.use_more_memory_to_avoid_reallocations();
			ns_image_properties prop;
			prop.components = 3;
			prop.width = graph_size.x- 2*border().x;
			prop.height = graph_size.y - 2*border().y;


			base_graph.init(prop);
			try {
				draw_base_graph(graph_contents);
				last_graph_contents = graph_contents;
			}
			catch (...) {
				base_graph.init(ns_image_properties(0, 0, 3));
				throw;
			}
		}
		//top margin
		for (unsigned int y = 0; y < border().y; y++)
			for (unsigned int x = 0; x < graph_size.x; x++)
				for (unsigned int c = 0; c < 3; c++) 
					buffer[map_pixel_from_image_onto_buffer(x, y, position, buffer_size) + c] = 0;
		
		for (unsigned int y = 0; y < base_graph.properties().height; y++) {
			//left margin
			for (unsigned int x = 0; x < border().x; x++)
				for (unsigned int c = 0; c < 3; c++)
					buffer[map_pixel_from_image_onto_buffer(x, y + border().y, position, buffer_size) + c] = 0;
			//graph
			for (unsigned int x = 0; x < base_graph.properties().width; x++) {
				for (unsigned int c = 0; c < 3; c++) 
					buffer[map_pixel_from_image_onto_buffer(x + border().x, y + border().y, position, buffer_size) + c] = base_graph[y][3 * x + c];
			}
			overlay_metadata(graph_contents,element_id, position, buffer_size, buffer);

			//right margin
			for (unsigned int x = base_graph.properties().width + border().x; x < graph_size.x; x++)
				for (unsigned int c = 0; c < 3; c++)
					buffer[map_pixel_from_image_onto_buffer(x, y + border().y, position, buffer_size) + c] = 0;
		}
		//top margin
		for (unsigned int y = border().y+ base_graph.properties().height; y < graph_size.y; y++)
			for (unsigned int x = 0; x < graph_size.x; x++)
				for (unsigned int c = 0; c < 3; c++)
					buffer[map_pixel_from_image_onto_buffer(x, y, position, buffer_size) + c] = 0;
	}
	void show(bool s) { _show = s; }
	bool show() const { return _show;  }
};

#endif
