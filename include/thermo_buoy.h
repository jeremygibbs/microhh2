/*
 * MicroHH
 * Copyright (c) 2011-2019 Chiel van Heerwaarden
 * Copyright (c) 2011-2019 Thijs Heus
 * Copyright (c) 2014-2019 Bart van Stratum
 *
 * This file is part of MicroHH
 *
 * MicroHH is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * MicroHH is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with MicroHH.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef THERMO_BUOY_H
#define THERMO_BUOY_H

#include "thermo.h"

class Master;
class Input;
class Netcdf_handle;
template<typename> class Grid;
template<typename> class Stats;
template<typename> class Diff;
template<typename> class Column;
template<typename> class Dump;
template<typename> class Cross;
template<typename> class Field3d;
template<typename> class Timeloop;

/**
 * Class for the dry thermodynamics.
 * This class is responsible for the computation of the right hand side term related to
 * the acceleration by buoyancy. In the dry thermodynamics temperature and buoyancy are
 * equivalent and no complex buoyancy function is required.
 */
template<typename TF>
class Thermo_buoy : public Thermo<TF>
{
    public:
        Thermo_buoy(Master&, Grid<TF>&, Fields<TF>&, Input&); ///< Constructor of the dry thermodynamics class.
        virtual ~Thermo_buoy(); ///< Destructor of the dry thermodynamics class.

        void exec(const double, Stats<TF>&); ///< Add the tendencies belonging to the buoyancy.
        void create(Input&, Netcdf_handle&, Stats<TF>&, Column<TF>&, Cross<TF>&, Dump<TF>&);
        unsigned long get_time_limit(unsigned long, double); ///< Compute the time limit (n/a for thermo_buoy)

        bool check_field_exists(std::string name);
        void get_buoyancy_surf(Field3d<TF>&, bool);             ///< Compute the near-surface and bottom buoyancy for usage in another routine.
        void get_buoyancy_fluxbot(Field3d<TF>&, bool);           ///< Compute the bottom buoyancy flux for usage in another routine.
        void get_T_bot(Field3d<TF>&, bool) { throw std::runtime_error("Function get_T_bot not implemented"); }
        void get_prog_vars(std::vector<std::string>&); ///< Retrieve a list of prognostic variables.
        void get_thermo_field(
                Field3d<TF>&, const std::string&, const bool, const bool); ///< Compute the buoyancy for usage in another routine.
        void get_radiation_fields(
                Field3d<TF>&, Field3d<TF>&, Field3d<TF>&, Field3d<TF>&, Field3d<TF>&) const
                { throw std::runtime_error("Function get_radiation_fields not implemented"); }
        const std::vector<TF>& get_p_vector() const { throw std::runtime_error("Function get_p_vector not implemented"); }
        const std::vector<TF>& get_ph_vector() const { throw std::runtime_error("Function get_ph_vector not implemented"); }
        const std::vector<TF>& get_exner_vector() const { throw std::runtime_error("Function get_exner_vector not implemented"); }
        int get_bl_depth();
        TF get_buoyancy_diffusivity();

        // Empty functions that are allowed to pass.
        void init() {}
        void exec_stats(Stats<TF>&) {};
        void exec_cross(Cross<TF>&, unsigned long) {};
        void exec_dump(Dump<TF>&, unsigned long) {};
        void exec_column(Column<TF>&) {};
        void get_mask(Stats<TF>&, std::string) {};
        bool has_mask(std::string) {return false;};

        void update_time_dependent(Timeloop<TF>&) {};

        #ifdef USECUDA
        void prepare_device() {};
        void clear_device() {};
        void forward_device() {};
        void backward_device() {};
        void get_thermo_field_g(Field3d<TF>&, const std::string&, const bool);
        void get_buoyancy_surf_g(Field3d<TF>&);
        void get_buoyancy_fluxbot_g(Field3d<TF>&);
        TF* get_basestate_fld_g(std::string) { throw std::runtime_error("Function get_basestate_fld_g not implemented"); };
        #endif

    private:
        using Thermo<TF>::swthermo;
        using Thermo<TF>::master;
        using Thermo<TF>::grid;
        using Thermo<TF>::fields;

        struct background_state
        {
            TF alpha;  ///< Slope angle in radians.
            TF n2;     ///< Background stratification.
            bool has_slope; ///< Boolean switch for slope flows
            bool has_N2;    ///< Boolean switch for imposed stratification
        };

        background_state bs;
        background_state bs_stats;

        bool swbaroclinic;
        TF dbdy_ls;
        const std::string tend_name = "buoy";
        const std::string tend_longname = "Buoyancy";
};
#endif
