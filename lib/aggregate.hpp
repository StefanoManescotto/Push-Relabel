// Copyright © 2024 Giorgio Audrito and Stefano Manescotto. All Rights Reserved.

/**
 * @file aggregate.hpp
 * @brief Implementation of the Aggregate Push-Relabel algorithm.
 */

//! Standard C++ imports.
#include <cassert>
#include <cmath>
#include <fstream>

//! Importing the FCPP library.
#include "lib/fcpp.hpp"

/**
 * @brief Namespace containing all the objects in the FCPP library.
 */
namespace fcpp {

//! @brief Dimensionality of the space.
constexpr size_t dim = 2;
//! @brief The size of the simulation area.
constexpr size_t area_size = 500;
//! @brief Convergence time.
constexpr size_t time_step = 200;

//! @brief Namespace containing the libraries of coordination routines.
namespace coordination {
//! @brief Tags used in the node storage.
namespace tags {
    //! @brief Color of the current node.
    struct node_color {};
    //! @brief Size of the current node.
    struct node_size {};
    //! @brief Shape of the current node.
    struct node_shape {};

    //! @brief Outward flow relative error at sinks
    struct sink_flow__error {};
    //! @brief Inward flow relative error at sources
    struct source_flow__error {};
    //! @brief Outward flow at sinks
    struct sink_flow {};
    //! @brief Inward flow at sources
    struct source_flow {};
    //! @brief Ideal exact flow
    struct ideal_flow {};
    //! @brief History of the ideal exact flow
    struct ideal_flow_history {};

    //! @brief Excess flow at each node
    struct excess_flow {};
    //! @brief Node heights
    struct node_height {};

    //! @brief Capacity of edges
    struct edge_capacities {};
    //! @brief Total number of nodes
    struct node_number {};
    //! @brief ID of the testcase
    struct test_id {};
}

//! @brief Function not moving devices if no graphical simulation.
FUN void disperser(ARGS, std::false_type) {}
//! @brief Function for moving devices according to the network topology.
FUN void disperser(ARGS, std::true_type) { CODE
    vec<2> v = neighbour_elastic_force(CALL, 0.2*area_size, 0.03) + point_elastic_force(CALL, make_vec(area_size,area_size)/2, 0, 0.005);
    if (isnan(v[0]) or isnan(v[1])) v = make_vec(0,0);
    node.velocity() = v;
}
//! @brief Export types used by the disperser function.
FUN_EXPORT disperser_t = export_list<neighbour_elastic_force_t, point_elastic_force_t>;

//! @brief Aggregate Push-Relabel algorithm, calculating excess flow and height of nodes.
FUN tuple<long long, int> aggregate_push_relabel(ARGS, bool is_source, bool is_sink, field<long long> capacity, int node_num) { CODE
    long long e_flow = 0;
    int height = 0;
    tuple<field<long long>, int, int> init(0, 0, 0);

    nbr(CALL, init, [&](field<tuple<long long, int, int>> flow_height){
        field<long long> new_flow = 0;

        field<long long> flow = -get<0>(flow_height);
        field<int> nbr_height = get<1>(flow_height);
        field<int> nbr_priority = get<2>(flow_height);

        height = get<1>(self(CALL, flow_height));
        field<bool> neigh_is_source = nbr(CALL, is_source);
        
        tuple<field<long long>, field<int>> o_flow(flow, nbr_priority);

        o_flow = old(CALL, o_flow, [&](field<tuple<long long, int>> old_values){
            field<long long> old_flow = get<0>(old_values);
            field<int> old_priority = get<1>(old_values);

            field<tuple<long long, int>> temp = map_hood([&](long long f, long long of, int priority, int h, int op, int uids){
                if(priority == op and op == 2 and f != of){
                    if(node.uid < uids){
                        return make_tuple(of, 2);
                    }
                    return make_tuple(f, 2);
                }

                if(f != of and height + 1 != h and priority == 1){
                    return make_tuple(of, 2);
                } else{
                    return make_tuple(f, 0);
                }
            }, flow, old_flow, nbr_priority, nbr_height, old_priority, nbr_uid(CALL));

            flow = get<0>(temp);
            nbr_priority = get<1>(temp);

            if (is_sink) {
                height = 0;
                flow = mux(flow > 0, 0ll, flow);
            }
            if (is_source) {
                height = node_num;
                flow = mux(nbr_height < height, capacity, mux(flow < 0, 0ll, flow));
            }

            flow = mux(flow > capacity, capacity, flow);
            e_flow = -sum_hood(CALL, flow, 0);

            // if a node is giving away more flow than it receives, stop giving excess (variant to try: reduce outgoing edges proportionally)
            if (not is_source and e_flow < 0) {
                flow = map_hood([&](long long f, int priority){
                    if (f > 0) {
                        long long r = min(-e_flow, f);
                        f -= r;
                        e_flow -= r;
                    }
                    return f;
                }, flow, nbr_priority);
            }
            
            field<long long> res_capacity = capacity - flow;

            field<int> neighs = nbr_uid(CALL);
            tuple<int, int> min_height = min_hood(CALL, mux(res_capacity > 0 and neighs != node.uid, make_tuple(nbr_height, neighs), make_tuple(INT_MAX, INT_MAX)));

            if (get<0>(min_height) >= height and e_flow > 0 and not is_source and not is_sink) {
                assert(min_height != make_tuple(INT_MAX, INT_MAX));
                height = get<0>(min_height) + 1; // Relabel
            }
            
            if (not is_source and not is_sink and e_flow > 0){
                new_flow = mux(get<1>(min_height) == neighs and height >= get<0>(min_height) + 1, min(res_capacity, e_flow), 0ll);
                nbr_priority = mux(new_flow > 0, 1, nbr_priority);
            }

            height = min_hood(CALL, mux(res_capacity > 0 and nbr_height + 1 < height, nbr_height, height));
            return make_tuple(flow + new_flow, nbr_priority);
        });
        flow = get<0>(o_flow);
        return make_tuple(flow, height, nbr_priority);
    });
    
    return make_tuple(e_flow, height);
}
//! @brief Export types used by the aggregate_push_relabel function.
FUN_EXPORT aggregate_push_relabel_t = export_list<tuple<field<long long>, int, field<int>>, field<long long>, bool, int, field<int>, tuple<field<long long>, field<int>>>;

//! @brief Main function.
template <bool graphic>
struct main {
    template <typename node_t>
    void operator()(node_t& node, times_t) {
        using namespace tags;
        disperser(CALL, std::integral_constant<bool,graphic>{});

        int node_num = node.net.storage(node_number{});
        field<long long> capacity = node.storage(edge_capacities{});
        bool is_source = (node.current_time() < 3*time_step and node.uid == 1) or (node.current_time() > 1*time_step and node.uid == 2);
        bool is_sink = (node.current_time() < 4*time_step and node.uid == node_num) or (node.current_time() > 2*time_step and node.uid == node_num-1);
        // bool is_sink = node.uid == node_num /*|| (node.uid == 2 && node.current_time() > 200)*/;
        // bool is_source = node.uid == 1;
        long long e_flow;
        int height;
        tie(e_flow, height) = aggregate_push_relabel(CALL, is_source, is_sink, capacity, node_num);


        long long ideal = node.net.storage(ideal_flow_history{})[node.current_time() / time_step];
        node.storage(ideal_flow{}) = ideal;
        node.storage(sink_flow{}) = is_sink ? e_flow : 0;
        node.storage(source_flow{}) = is_source ? -e_flow : 0;
        node.storage(excess_flow{}) = e_flow;
        node.storage(node_height{}) = height;

        if (graphic) {
            node.storage(node_size{}) = is_sink or is_source ? 15 : 5 + min(e_flow * 1.0 / ideal, 1.0) * 20;
            node.storage(node_color{}) = color::hsva(height * 360 / node_num, 1, 1);
            node.storage(node_shape{}) = is_sink ? shape::star : is_source ? shape::cube : shape::sphere;
        }
    }
};
//! @brief Export types used by the MAIN function.
FUN_EXPORT main_t = export_list<disperser_t, aggregate_push_relabel_t>;

} // namespace coordination

// [SYSTEM SETUP]

namespace functor {
    //! @brief Functor computing the relative error of V with respect to I, from 0% to 100%.
    template <typename V, typename I>
    using error = div<abs<sub<I,V>>, max<max<I, distribution::constant_n<int,1>>,V>>;
}

//! @brief Namespace for component options.
namespace option {

//! @brief Import tags to be used for component options.
using namespace component::tags;
//! @brief Import tags used by aggregate functions.
using namespace coordination::tags;

//! @brief When to end the simulation.
constexpr size_t end = 5*time_step;

//! @brief Description of the round schedule.
template <bool sync>
using round_s = std::conditional_t<
    sync,
    sequence::periodic_n<2, 1, 2, 2*end+2>,          // one round at every half second
    sequence::periodic<
        distribution::interval_n<times_t, 0, 1>,     // uniform time in the [0,1] interval for start
        distribution::weibull_n<times_t, 10, 1, 10>, // weibull-distributed time for interval (10/10=1 mean, 1/10=0.1 deviation)
        distribution::constant_n<times_t, end+3>
    >
>;
//! @brief The sequence of network snapshots (one every simulated second).
using log_s = sequence::periodic_n<1, 1, 1, end>;
//! @brief The distribution of initial node positions (random in a square).
using rectangle_d = distribution::rect_n<1, 0, 0, area_size, area_size>;
//! @brief The contents of the node storage as tags and associated types.
using store_t = node_store<
    node_color,                 color,
    node_size,                  double,
    node_shape,                 shape,
    sink_flow,                  long long,
    source_flow,                long long,
    ideal_flow,                 long long,
    edge_capacities,            field<long long>,
    excess_flow,                long long,
    node_height,                int
>;
//! @brief The tags and corresponding aggregators to be logged.
using aggregator_t = aggregators<
    sink_flow,      aggregator::sum<long long>,
    source_flow,    aggregator::sum<long long>,
    ideal_flow,     aggregator::max<real_t>
>;

//! @brief The tags and functors computing derived properties to be logged.
using functors_t = log_functors<
    sink_flow__error,   functor::error<aggregator::sum<sink_flow>, aggregator::max<ideal_flow>>
>;

//! @brief Helper template for plotting multiple lines.
template <typename... Ts>
using lines_t = plot::join<plot::value<Ts>...>;

//! @brief Plot with absolute flow values.
using absolute_plot = plot::split<plot::time, lines_t<aggregator::sum<sink_flow>, aggregator::max<ideal_flow>>>;

//! @brief Plot with absolute flow values.
using relative_plot = plot::split<plot::time, lines_t<sink_flow__error>>;

//! @brief Overall plot description.
using plot_row = plot::join<absolute_plot, relative_plot>;

//! @brief Overall plot description.
using plot_t = plot::join<plot_row, plot::split<test_id, plot_row>>;

//! @brief The general simulation options.
template <bool par, bool sync, bool gui>
DECLARE_OPTIONS(list,
    parallel<par>,                      // multithreading enabled on node rounds
    synchronised<sync>,                 // optimise for asynchronous networks
    program<coordination::main<gui>>,   // program to be run (refers to MAIN above)
    exports<coordination::main_t>,      // export type list (types used in messages)
    retain<metric::retain<2,1>>,        // messages are kept for 2 seconds before expiring
    round_schedule<round_s<sync>>,      // the sequence generator for round events on nodes
    log_schedule<log_s>,                // the sequence generator for log events on the network
    net_store<                          // overall parameters stored at the network level
        node_number,        int,
        ideal_flow_history, std::vector<long long>
    >,
    store_t,            // the contents of the node storage
    aggregator_t,       // the tags and corresponding aggregators to be logged
    functors_t,         // description of derived quantities to be logged
    plot_type<plot_t>,  // the plot description to be used
    area<0, 0, area_size, area_size>,   // the simulation area
    init<                   // random node initialization
        x,          rectangle_d
    >,
    node_attributes<        // node initialization from file
        uid,                device_t,
        edge_capacities,    field<long long>
    >,
    extra_info<             // additional information for plotting
        test_id,    int
    >,
    dimension<dim>,         // dimensionality of the space
    shape_tag<node_shape>,  // the shape of a node is read from this tag in the store
    size_tag<node_size>,    // the size  of a node is read from this tag in the store
    color_tag<node_color>   // the color of a node is read from this tag in the store
);

} // namespace option

} // namespace fcpp

//! @brief Reads a single number from a file
int file_to_number(std::string path){
    std::ifstream in(path);
    int n_nodes;
    in >> n_nodes;
    return n_nodes;
}