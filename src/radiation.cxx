/*
 * MicroHH
 * Copyright (c) 2011-2018 Chiel van Heerwaarden
 * Copyright (c) 2011-2018 Thijs Heus
 * Copyright (c) 2014-2018 Bart van Stratum
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

#include <vector>
#include <string>
#include <iostream>
#include <boost/algorithm/string.hpp>

#include "radiation.h"
#include "master.h"
#include "grid.h"
#include "fields.h"
#include "thermo.h"
#include "input.h"
#include "netcdf_interface.h"

#include "Gas_concs.h"

namespace
{
}

template<typename TF>
Radiation<TF>::Radiation(Master& masterin, Grid<TF>& gridin, Fields<TF>& fieldsin, Input& inputin) :
    master(masterin), grid(gridin), fields(fieldsin), field3d_operators(master, grid, fields)
{
    // Read the switches from the input
    std::string swradiation_in = inputin.get_item<std::string>("radiation", "swradiation", "", "0");

    if (swradiation_in == "0")
        swradiation = Radiation_type::Disabled;
    else if (swradiation_in == "1")
        swradiation = Radiation_type::Enabled;
    else
        throw std::runtime_error("Invalid option for \"swradiation\"");
}

template<typename TF>
Radiation<TF>::~Radiation()
{
}

template<typename TF>
void Radiation<TF>::init()
{
    if (swradiation == Radiation_type::Disabled)
        return;
}

template<typename TF>
void Radiation<TF>::create(Thermo<TF>& thermo, Netcdf_handle& input_nc)
{
    if (swradiation == Radiation_type::Disabled)
        return;

    Netcdf_group group_nc = input_nc.get_group("radiation");

    Netcdf_file coef_lw_nc(master, "coefficients_lw.nc", Netcdf_mode::Read);

    // Get the gas names.
    std::vector<std::string> gas_names;
    std::map<std::string, int> dims = coef_lw_nc.get_variable_dimensions("gas_names");

    int n_adsorber = dims.at("absorber");
    int n_char = dims.at("string_len");
    for (int n=0; n<n_adsorber; ++n)
    {
        std::vector<char> gas_name_char(n_char);
        coef_lw_nc.get_variable(gas_name_char, "gas_names", {n,0}, {1,n_char});
        std::string gas_name(gas_name_char.begin(), gas_name_char.end());
        boost::trim(gas_name);
        gas_names.push_back(gas_name);
    }

    int layer = group_nc.get_variable_dimensions("pres_layer").at("layer");
    int level = group_nc.get_variable_dimensions("pres_level").at("level");

    // Download pressure and temperature data.
    pres_layer.resize(layer);
    pres_level.resize(level);
    temp_layer.resize(layer);
    temp_level.resize(level);

    group_nc.get_variable(pres_layer, "pres_layer", {0}, {layer});
    group_nc.get_variable(pres_level, "pres_level", {0}, {level});
    group_nc.get_variable(temp_layer, "temp_layer", {0}, {layer});
    group_nc.get_variable(temp_level, "temp_level", {0}, {level});

    // Read the gas concentrations.
    std::vector<Gas_concs<TF>> gas_conc_array;

    // Read the gas concentrations.

    // Download surface boundary conditions for long wave.
    surface_emissivity.resize(1);
    surface_temperature.resize(1);

    group_nc.get_variable(surface_emissivity, "surface_emissivity", {0}, {1});
    group_nc.get_variable(surface_temperature, "surface_temperature", {0}, {1});

    throw 666;
}

template<typename TF>
void Radiation<TF>::exec(Thermo<TF>& thermo)
{
    if (swradiation == Radiation_type::Disabled)
        return;
}

template class Radiation<double>;
template class Radiation<float>;
