/*
  Copyright (C) 2011 - 2017 by the authors of the ASPECT code.

  This file is part of ASPECT.

  ASPECT is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.

  ASPECT is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with ASPECT; see the file doc/COPYING.  If not see
  <http://www.gnu.org/licenses/>.
*/

#include <aspect/material_model/compositing.h>

namespace aspect
{
  namespace MaterialModel
  {
    // Parse property names
    template <int dim>
    Property::MaterialProperty
    Compositing<dim>::parse_property_name(const std::string &s)
    {
      AssertThrow (Property::property_map.count(s) > 0,
                   ExcMessage ("The value <" + s + "> for a material "
                               "property is not one of the valid values."));
      return Property::property_map.at(s);
    }

    template <int dim>
    void
    Compositing<dim>::copy_required_properties(const unsigned int model_index,
                                               const typename Interface<dim>::MaterialModelOutputs &base_output,
                                               typename Interface<dim>::MaterialModelOutputs &out) const
    {
      if (model_property_map.at(Property::viscosity) == model_index)
        out.viscosities = base_output.viscosities;
      if (model_property_map.at(Property::density) == model_index)
        out.densities = base_output.densities;
      if (model_property_map.at(Property::thermal_expansion_coefficient) == model_index)
        out.thermal_expansion_coefficients = base_output.thermal_expansion_coefficients;
      if (model_property_map.at(Property::specific_heat) == model_index)
        out.specific_heat = base_output.specific_heat;
      if (model_property_map.at(Property::thermal_conductivity) == model_index)
        out.thermal_conductivities = base_output.thermal_conductivities;
      if (model_property_map.at(Property::compressibility) == model_index)
        out.compressibilities = base_output.compressibilities;
      if (model_property_map.at(Property::entropy_derivative_pressure) == model_index)
        out.entropy_derivative_pressure = base_output.entropy_derivative_pressure;
      if (model_property_map.at(Property::entropy_derivative_temperature) == model_index)
        out.entropy_derivative_temperature = base_output.entropy_derivative_temperature;
      if (model_property_map.at(Property::reaction_terms) == model_index)
        out.reaction_terms = base_output.reaction_terms;
    }

    template <int dim>
    void
    Compositing<dim>::evaluate(const typename Interface<dim>::MaterialModelInputs &in,
                               typename Interface<dim>::MaterialModelOutputs &out) const
    {
      typename Interface<dim>::MaterialModelOutputs base_output(out.viscosities.size(),
                                                                this->introspection().n_compositional_fields);

      for (unsigned int i=0; i<models.size(); ++i)
        {
          models[i]->evaluate(in, base_output);
          copy_required_properties(i, base_output, out);
        }
    }

    template <int dim>
    void
    Compositing<dim>::declare_parameters (ParameterHandler &prm)
    {
      prm.enter_subsection("Material model");
      {
        prm.enter_subsection("Compositing");
        {
          std::map<std::string, Property::MaterialProperty>::const_iterator prop_it = Property::property_map.begin();
          for (; prop_it != Property::property_map.end(); ++prop_it)
            {
              prm.declare_entry(prop_it->first, "unspecified",
                                Patterns::Selection(
                                  MaterialModel::get_valid_model_names_pattern<dim>()+"|unspecified"
                                ),
                                "Material model to use for " + prop_it->first +". Valid values for this "
                                "parameter are the names of models that are also valid for the "
                                "``Material models/Model name'' parameter. See the documentation for "
                                "that for more information.");
            }
        }
        prm.leave_subsection();
      }
      prm.leave_subsection();
    }

    template <int dim>
    void
    Compositing<dim>::parse_parameters (ParameterHandler &prm)
    {
      prm.enter_subsection("Material model");
      {
        prm.enter_subsection("Compositing");
        {
          model_names.clear();
          std::map<std::string, Property::MaterialProperty>::const_iterator prop_it = Property::property_map.begin();
          for (; prop_it != Property::property_map.end(); ++prop_it)
            {
              const Property::MaterialProperty prop = prop_it->second;
              const std::string model_name = prm.get(prop_it->first);

              AssertThrow(model_name != "averaging",
                          ExcMessage("You may not use ``averaging'' as the base model for the "
                                     + prop_it->first +" property of a compositing material model."));
              AssertThrow(model_name != "compositing",
                          ExcMessage("You may not use ``compositing'' as the base model for the "
                                     + prop_it->first +" property of a compositing material model."));
              std::vector<std::string>::iterator model_position = std::find(model_names.begin(), model_names.end(), model_name);
              if ( model_position == model_names.end() )
                {
                  model_property_map[prop] = model_names.size();
                  model_names.push_back(model_name);
                }
              else
                model_property_map[prop] = std::distance(model_names.begin(), model_position);
            }

        }
        prm.leave_subsection();
      }
      prm.leave_subsection();

      // create the models and initialize their SimulatorAccess base
      // After parsing the parameters for averaging, it is essential to parse
      // parameters related to the base models
      models.resize(model_names.size());
      for (unsigned int i=0; i<model_names.size(); ++i)
        {
          models[i].reset(create_material_model<dim>(model_names[i]));
          if (SimulatorAccess<dim> *sim = dynamic_cast<SimulatorAccess<dim>*>(models[i].get()))
            sim->initialize_simulator (this->get_simulator());
          models[i]->parse_parameters(prm);
          // All models will need to compute all quantities, so do so
          this->model_dependence.viscosity |= models[i]->get_model_dependence().viscosity;
          this->model_dependence.density |= models[i]->get_model_dependence().density;
          this->model_dependence.compressibility |= models[i]->get_model_dependence().compressibility;
          this->model_dependence.specific_heat |= models[i]->get_model_dependence().specific_heat;
          this->model_dependence.thermal_conductivity |= models[i]->get_model_dependence().thermal_conductivity;
        }
    }

    template <int dim>
    bool
    Compositing<dim>::
    is_compressible () const
    {
      const unsigned int ind = model_property_map.at(Property::compressibility);
      return models[ind]->is_compressible();
    }

    template <int dim>
    double
    Compositing<dim>::
    reference_viscosity() const
    {
      const unsigned int ind = model_property_map.at(Property::viscosity);
      return models[ind]->reference_viscosity();
    }
  }
}

// explicit instantiations
namespace aspect
{
  namespace MaterialModel
  {
    ASPECT_REGISTER_MATERIAL_MODEL(Compositing,
                                   "compositing",
                                   "The ``compositing'' Material model selects material model properties from a "
                                   "given set of other material models, and is intended to make mixing different "
                                   "material models easier. However, the implementation is somewhat expensive, "
                                   "and it is therefore likely to be preferred that a separate implementation be "
                                   "used if computing speed is critical."
                                  )
  }
}