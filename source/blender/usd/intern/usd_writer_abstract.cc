#include "usd_writer_abstract.h"
#include "usd_hierarchy_iterator.h"

#include <pxr/base/tf/stringUtils.h>

extern "C" {
#include "BKE_animsys.h"
#include "BKE_key.h"

#include "DNA_modifier_types.h"
}

USDAbstractWriter::USDAbstractWriter(const USDExporterContext &usd_export_context)
    : usd_export_context_(usd_export_context), frame_has_been_written_(false), is_animated_(false)
{
}

USDAbstractWriter::~USDAbstractWriter()
{
}

bool USDAbstractWriter::is_supported(const Object * /*object*/) const
{
  return true;
}

pxr::UsdTimeCode USDAbstractWriter::get_export_time_code() const
{
  if (is_animated_) {
    return usd_export_context_.hierarchy_iterator->get_export_time_code();
  }
  // By using the default timecode USD won't even write a single `timeSample` for non-animated
  // data. Instead, it writes it as non-timesampled.
  static pxr::UsdTimeCode default_timecode = pxr::UsdTimeCode::Default();
  return default_timecode;
}

void USDAbstractWriter::write(HierarchyContext &context)
{
  if (!frame_has_been_written_) {
    is_animated_ = usd_export_context_.export_params.export_animation &&
                   check_is_animated(context);
  }
  else if (!is_animated_) {
    /* A frame has already been written, and without animation one frame is enough. */
    return;
  }

  do_write(context);

  frame_has_been_written_ = true;
}

bool USDAbstractWriter::check_is_animated(const HierarchyContext &context) const
{
  const Object *object = context.object;

  if (object->data != nullptr) {
    AnimData *adt = BKE_animdata_from_id(static_cast<ID *>(object->data));
    /* TODO(Sybren): make this check more strict, as the AnimationData may
     * actually be empty (no fcurves, drivers, etc.) and thus effectively
     * have no animation at all. */
    if (adt != nullptr) {
      return true;
    }
  }
  if (BKE_key_from_object(object) != nullptr) {
    return true;
  }

  /* Test modifiers. */
  ModifierData *md = static_cast<ModifierData *>(object->modifiers.first);
  while (md) {
    if (md->type != eModifierType_Subsurf) {
      return true;
    }
    md = md->next;
  }

  return false;
}

const pxr::SdfPath &USDAbstractWriter::usd_path() const
{
  return usd_export_context_.usd_path;
}

pxr::UsdShadeMaterial USDAbstractWriter::ensure_usd_material(Material *material)
{
  static pxr::SdfPath material_library_path("/_materials");
  pxr::UsdStageRefPtr stage = usd_export_context_.stage;

  // Construct the material.
  pxr::TfToken material_name(usd_export_context_.hierarchy_iterator->get_id_name(&material->id));
  pxr::SdfPath usd_path = material_library_path.AppendChild(material_name);
  pxr::UsdShadeMaterial usd_material = pxr::UsdShadeMaterial::Get(stage, usd_path);
  if (usd_material) {
    return usd_material;
  }
  usd_material = pxr::UsdShadeMaterial::Define(stage, usd_path);

  // Construct the shader.
  pxr::SdfPath shader_path = usd_path.AppendChild(pxr::TfToken("previewShader"));
  pxr::UsdShadeShader shader = pxr::UsdShadeShader::Define(stage, shader_path);
  shader.CreateIdAttr(pxr::VtValue(pxr::TfToken("UsdPreviewSurface")));
  shader.CreateInput(pxr::TfToken("diffuseColor"), pxr::SdfValueTypeNames->Color3f)
      .Set(pxr::GfVec3f(material->r, material->g, material->b));
  shader.CreateInput(pxr::TfToken("roughness"), pxr::SdfValueTypeNames->Float)
      .Set(material->roughness);
  shader.CreateInput(pxr::TfToken("metallic"), pxr::SdfValueTypeNames->Float)
      .Set(material->metallic);

  // Connect the shader and the material together.
  usd_material.CreateSurfaceOutput().ConnectToSource(shader, pxr::TfToken("surface"));

  return usd_material;
}
