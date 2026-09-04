#include "SkeletonData.h"

// Spine 4.3 JSON reader with 4.2-compatible downgrade mapping.
// Format reference: spine-runtimes 4.3 spine-cpp/src/spine/SkeletonJson.cpp.
//
// Downgrade decisions (4.3 -> universal SkeletonData, written out as 4.2):
// - constraints: unified array with "type"; order = array index. "slider" dropped.
// - ik: "scaleY" enum (none/uniform/volume) -> uniform bool; volume maps to true (lossy).
// - transform: "source" -> target; "localTarget" -> local; "additive" -> relative
//   (exact: applyRelativeWorld == ToX::apply additive branch; falls back to
//   "localSource" approx when key absent); "clamp" dropped. "properties" from->to: same-property pairs keep
//   offset (flat key + from.offset) and mix (flat mix * to.scale); to.offset/to.max and
//   cross-property pairs dropped (no 4.2 equivalent). Properties not enabled -> mix 0
//   (4.3 pose defaults are 0, so they are not applied).
// - path: "slot" -> target.
// - physics: setup defaults changed in 4.3 (inertia 0.5, damping 0.85) so omitted keys
//   materialize explicitly when written as 4.2 (4.2 defaults are 1). "scaleY" dropped.
// - linkedmesh: "source" -> parent; cross-slot "slot" not representable in 4.2 -> attachment dropped.
// - sequence: "setup" -> setupIndex.
// - bone iconSize/iconRotation, skin color, skin "slider" refs, clipping convex/inverse,
//   drawOrderFolder, slider animations, constraint "animation" refs: dropped.

namespace spine43 {

Sequence readSequence(const Json& j) {
    Sequence sequence;
    sequence.count = j.value("count", 0);
    sequence.start = j.value("start", 1);
    sequence.digits = j.value("digits", 0);
    sequence.setupIndex = j.value("setup", 0);
    return sequence;
}

void readCurve(const Json& j, TimelineFrame& frame) {
    if (j.contains("curve")) {
        if (j["curve"] == "stepped") {
            frame.curveType = CurveType::CURVE_STEPPED;
        } else {
            frame.curveType = CURVE_BEZIER;
            frame.curve = j["curve"].get<std::vector<float>>();
        }
    }
}

void readTimeline(const Json& j, Timeline& timeline, int valueNum, const std::string& key1, const std::string& key2, float defaultValue) {
    for (const auto& frameJson : j) {
        TimelineFrame frame;
        frame.time = frameJson.value("time", 0.0f);
        frame.value1 = frameJson.value(key1, defaultValue);
        if (valueNum > 1) frame.value2 = frameJson.value(key2, defaultValue);
        readCurve(frameJson, frame);
        timeline.push_back(frame);
    }
}

IKConstraintData readIkConstraint(const Json& ikJson, int order) {
    IKConstraintData ikData;
    if (ikJson.contains("name")) ikData.name = ikJson["name"];
    ikData.order = order;
    ikData.skinRequired = ikJson.value("skin", false);
    if (ikJson.contains("bones")) ikData.bones = ikJson["bones"].get<std::vector<std::string>>();
    if (ikJson.contains("target")) ikData.target = ikJson["target"];
    ikData.mix = ikJson.value("mix", 1.0f);
    ikData.softness = ikJson.value("softness", 0.0f);
    ikData.bendPositive = ikJson.value("bendPositive", true);
    ikData.compress = ikJson.value("compress", false);
    ikData.stretch = ikJson.value("stretch", false);
    // 4.3 default scaleY mode is "none" -> uniform false
    ikData.uniform = ikJson.contains("scaleY") && ikJson["scaleY"] != "none";
    return ikData;
}

TransformConstraintData readTransformConstraint(const Json& transformJson, int order) {
    TransformConstraintData transformData;
    if (transformJson.contains("name")) transformData.name = transformJson["name"];
    transformData.order = order;
    transformData.skinRequired = transformJson.value("skin", false);
    if (transformJson.contains("bones")) transformData.bones = transformJson["bones"].get<std::vector<std::string>>();
    if (transformJson.contains("source")) transformData.target = transformJson["source"];

    static const char* propNames[6] = {"rotate", "x", "y", "scaleX", "scaleY", "shearY"};
    bool enabled[6] = {false, false, false, false, false, false};
    float gain[6] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    float extraOffset[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    if (transformJson.contains("properties")) {
        const Json& properties = transformJson["properties"];
        for (int p = 0; p < 6; p++) {
            if (!properties.contains(propNames[p])) continue;
            const Json& from = properties[propNames[p]];
            extraOffset[p] = from.value("offset", 0.0f);
            if (!from.contains("to")) continue;
            const Json& to = from["to"];
            if (to.contains(propNames[p])) {
                enabled[p] = true;
                gain[p] = to[propNames[p]].value("scale", 1.0f);
            }
        }
    }

    transformData.offsetRotation = transformJson.value("rotation", 0.0f) + extraOffset[0];
    transformData.offsetX = transformJson.value("x", 0.0f) + extraOffset[1];
    transformData.offsetY = transformJson.value("y", 0.0f) + extraOffset[2];
    transformData.offsetScaleX = transformJson.value("scaleX", 0.0f) + extraOffset[3];
    transformData.offsetScaleY = transformJson.value("scaleY", 0.0f) + extraOffset[4];
    transformData.offsetShearY = transformJson.value("shearY", 0.0f) + extraOffset[5];

    float mixX = transformJson.value("mixX", 1.0f);
    float mixScaleX = transformJson.value("mixScaleX", 1.0f);
    transformData.mixRotate = enabled[0] ? transformJson.value("mixRotate", 1.0f) * gain[0] : 0.0f;
    transformData.mixX = enabled[1] ? mixX * gain[1] : 0.0f;
    transformData.mixY = enabled[2] ? transformJson.value("mixY", mixX) * gain[2] : 0.0f;
    transformData.mixScaleX = enabled[3] ? mixScaleX * gain[3] : 0.0f;
    transformData.mixScaleY = enabled[4] ? transformJson.value("mixScaleY", mixScaleX) * gain[4] : 0.0f;
    transformData.mixShearY = enabled[5] ? transformJson.value("mixShearY", 1.0f) * gain[5] : 0.0f;

    transformData.local = transformJson.value("localTarget", false);
    // 4.3 additive is the exact equivalent of 4.2 relative (applyRelativeWorld ==
    // ToX::apply additive branch). Prefer it; fall back to localSource approximation
    // only when the key is absent (pre-4.3-style data).
    transformData.relative = transformJson.contains("additive")
        ? transformJson.value("additive", false)
        : transformJson.value("localSource", false);
    return transformData;
}

PathConstraintData readPathConstraint(const Json& pathJson, int order) {
    PathConstraintData pathData;
    if (pathJson.contains("name")) pathData.name = pathJson["name"];
    pathData.order = order;
    pathData.skinRequired = pathJson.value("skin", false);
    if (pathJson.contains("bones")) pathData.bones = pathJson["bones"].get<std::vector<std::string>>();
    if (pathJson.contains("slot")) pathData.target = pathJson["slot"];
    pathData.positionMode = positionModeMap.at(pathJson.value("positionMode", "percent"));
    pathData.spacingMode = spacingModeMap.at(pathJson.value("spacingMode", "length"));
    pathData.rotateMode = rotateModeMap.at(pathJson.value("rotateMode", "tangent"));
    pathData.offsetRotation = pathJson.value("rotation", 0.0f);
    pathData.position = pathJson.value("position", 0.0f);
    pathData.spacing = pathJson.value("spacing", 0.0f);
    pathData.mixRotate = pathJson.value("mixRotate", 1.0f);
    pathData.mixX = pathJson.value("mixX", 1.0f);
    pathData.mixY = pathJson.value("mixY", pathData.mixX);
    return pathData;
}

PhysicsConstraintData readPhysicsConstraint(const Json& physicsJson, int order) {
    PhysicsConstraintData physicsData;
    if (physicsJson.contains("name")) physicsData.name = physicsJson["name"];
    physicsData.order = order;
    physicsData.skinRequired = physicsJson.value("skin", false);
    if (physicsJson.contains("bone")) physicsData.bone = physicsJson["bone"];
    physicsData.x = physicsJson.value("x", 0.0f);
    physicsData.y = physicsJson.value("y", 0.0f);
    physicsData.rotate = physicsJson.value("rotate", 0.0f);
    physicsData.scaleX = physicsJson.value("scaleX", 0.0f);
    physicsData.shearX = physicsJson.value("shearX", 0.0f);
    physicsData.limit = physicsJson.value("limit", 5000.0f);
    physicsData.fps = physicsJson.value("fps", 60.0f);
    physicsData.inertia = physicsJson.value("inertia", 0.5f);
    physicsData.strength = physicsJson.value("strength", 100.0f);
    physicsData.damping = physicsJson.value("damping", 0.85f);
    physicsData.mass = physicsJson.value("mass", 1.0f);
    physicsData.wind = physicsJson.value("wind", 0.0f);
    physicsData.gravity = physicsJson.value("gravity", 0.0f);
    physicsData.mix = physicsJson.value("mix", 1.0f);
    physicsData.inertiaGlobal = physicsJson.value("inertiaGlobal", false);
    physicsData.strengthGlobal = physicsJson.value("strengthGlobal", false);
    physicsData.dampingGlobal = physicsJson.value("dampingGlobal", false);
    physicsData.massGlobal = physicsJson.value("massGlobal", false);
    physicsData.windGlobal = physicsJson.value("windGlobal", false);
    physicsData.gravityGlobal = physicsJson.value("gravityGlobal", false);
    physicsData.mixGlobal = physicsJson.value("mixGlobal", false);
    return physicsData;
}

SkeletonData readJsonData(const Json& j) {
    SkeletonData skeletonData;

    const auto& skeleton = j["skeleton"];
    // 4.3 writes explicit nulls (e.g. "audio": null) where 4.2 omits the key
    skeletonData.hash = (skeleton.contains("hash") && !skeleton["hash"].is_null()) ? base64ToUint64(skeleton["hash"]) : 0;
    if (skeleton.contains("spine") && !skeleton["spine"].is_null()) skeletonData.version = skeleton["spine"];
    skeletonData.x = skeleton.value("x", 0.0f);
    skeletonData.y = skeleton.value("y", 0.0f);
    skeletonData.width = skeleton.value("width", 0.0f);
    skeletonData.height = skeleton.value("height", 0.0f);
    skeletonData.referenceScale = skeleton.value("referenceScale", 100.0f);
    skeletonData.fps = skeleton.value("fps", 30.0f);
    if (skeleton.contains("images") && !skeleton["images"].is_null()) skeletonData.imagesPath = skeleton["images"];
    if (skeleton.contains("audio") && !skeleton["audio"].is_null()) skeletonData.audioPath = skeleton["audio"];
    skeletonData.nonessential = !(skeletonData.fps == 30.0f && skeletonData.imagesPath == std::nullopt && skeletonData.audioPath == std::nullopt);

    /* Bones */
    if (j.contains("bones")) {
        for (const auto& boneJson : j["bones"]) {
            BoneData boneData;
            if (boneJson.contains("name")) boneData.name = boneJson["name"];
            if (boneJson.contains("parent")) boneData.parent = boneJson["parent"];
            boneData.length = boneJson.value("length", 0.0f);
            boneData.x = boneJson.value("x", 0.0f);
            boneData.y = boneJson.value("y", 0.0f);
            boneData.rotation = boneJson.value("rotation", 0.0f);
            boneData.scaleX = boneJson.value("scaleX", 1.0f);
            boneData.scaleY = boneJson.value("scaleY", 1.0f);
            boneData.shearX = boneJson.value("shearX", 0.0f);
            boneData.shearY = boneJson.value("shearY", 0.0f);
            boneData.inherit = inheritMap.at(boneJson.value("inherit", "normal"));
            boneData.skinRequired = boneJson.value("skin", false);
            if (boneJson.contains("color")) boneData.color = stringToColor(boneJson["color"], true);
            boneData.icon = boneJson.value("icon", "");
            boneData.visible = boneJson.value("visible", true);
            skeletonData.bones.push_back(boneData);
        }
    }

    /* Slots */
    if (j.contains("slots")) {
        for (const auto& slotJson : j["slots"]) {
            SlotData slotData;
            if (slotJson.contains("name")) slotData.name = slotJson["name"];
            if (slotJson.contains("bone")) slotData.bone = slotJson["bone"];
            if (slotJson.contains("color")) slotData.color = stringToColor(slotJson["color"], true);
            if (slotJson.contains("dark")) slotData.darkColor = stringToColor(slotJson["dark"], false);
            if (slotJson.contains("attachment") && !slotJson["attachment"].is_null()) slotData.attachmentName = slotJson["attachment"];
            slotData.blendMode = blendModeMap.at(slotJson.value("blend", "normal"));
            slotData.visible = slotJson.value("visible", true);
            skeletonData.slots.push_back(slotData);
        }
    }

    /* Constraints: unified array, order = index. Slider dropped. */
    if (j.contains("constraints")) {
        int order = 0;
        for (const auto& constraintJson : j["constraints"]) {
            std::string type = constraintJson.value("type", "");
            if (type == "ik") {
                skeletonData.ikConstraints.push_back(readIkConstraint(constraintJson, order));
            } else if (type == "transform") {
                skeletonData.transformConstraints.push_back(readTransformConstraint(constraintJson, order));
            } else if (type == "path") {
                skeletonData.pathConstraints.push_back(readPathConstraint(constraintJson, order));
            } else if (type == "physics") {
                skeletonData.physicsConstraints.push_back(readPhysicsConstraint(constraintJson, order));
            }
            order++;
        }
    }

    /* Skins */
    if (j.contains("skins")) {
        for (const auto& skinJson : j["skins"]) {
            Skin skinData;
            skinData.name = skinJson.value("name", "");
            if (skinJson.contains("bones")) skinData.bones = skinJson["bones"].get<std::vector<std::string>>();
            if (skinJson.contains("ik")) skinData.ik = skinJson["ik"].get<std::vector<std::string>>();
            if (skinJson.contains("transform")) skinData.transform = skinJson["transform"].get<std::vector<std::string>>();
            if (skinJson.contains("path")) skinData.path = skinJson["path"].get<std::vector<std::string>>();
            if (skinJson.contains("physics")) skinData.physics = skinJson["physics"].get<std::vector<std::string>>();
            if (skinJson.contains("attachments")) {
                for (const auto& [slotName, slotAttachments] : skinJson["attachments"].items()) {
                    skinData.attachments[slotName] = {};
                    for (const auto& [attachmentName, attachmentJson] : slotAttachments.items()) {
                        Attachment attachment;
                        attachment.name = attachmentJson.value("name", attachmentName);
                        attachment.path = attachmentJson.value("path", attachment.name);
                        attachment.type = attachmentTypeMap.at(attachmentJson.value("type", "region"));
                        if ((attachment.type == AttachmentType_Mesh || attachment.type == AttachmentType_Linkedmesh)
                            && attachmentJson.contains("source")) {
                            // Cross-slot linked meshes have no 4.2 representation; drop the attachment.
                            if (attachmentJson.contains("slot") && attachmentJson["slot"] != slotName) continue;
                            LinkedmeshAttachment linkedMesh;
                            linkedMesh.width = attachmentJson.value("width", 32.0f);
                            linkedMesh.height = attachmentJson.value("height", 32.0f);
                            if (attachmentJson.contains("color")) linkedMesh.color = stringToColor(attachmentJson["color"], true);
                            if (attachmentJson.contains("sequence")) linkedMesh.sequence = readSequence(attachmentJson["sequence"]);
                            linkedMesh.parentMesh = attachmentJson["source"];
                            linkedMesh.timelines = attachmentJson.value("timelines", true) ? 1 : 0;
                            if (attachmentJson.contains("skin")) linkedMesh.skin = attachmentJson["skin"];
                            attachment.data = linkedMesh;
                            skinData.attachments[slotName][attachmentName] = attachment;
                            continue;
                        }
                        switch (attachment.type) {
                            case AttachmentType_Region: {
                                RegionAttachment region;
                                region.x = attachmentJson.value("x", 0.0f);
                                region.y = attachmentJson.value("y", 0.0f);
                                region.rotation = attachmentJson.value("rotation", 0.0f);
                                region.scaleX = attachmentJson.value("scaleX", 1.0f);
                                region.scaleY = attachmentJson.value("scaleY", 1.0f);
                                region.width = attachmentJson.value("width", 32.0f);
                                region.height = attachmentJson.value("height", 32.0f);
                                if (attachmentJson.contains("color")) region.color = stringToColor(attachmentJson["color"], true);
                                if (attachmentJson.contains("sequence")) region.sequence = readSequence(attachmentJson["sequence"]);
                                attachment.data = region;
                                break;
                            }
                            case AttachmentType_Mesh: {
                                MeshAttachment mesh;
                                mesh.width = attachmentJson.value("width", 32.0f);
                                mesh.height = attachmentJson.value("height", 32.0f);
                                if (attachmentJson.contains("color")) mesh.color = stringToColor(attachmentJson["color"], true);
                                if (attachmentJson.contains("sequence")) mesh.sequence = readSequence(attachmentJson["sequence"]);
                                mesh.hullLength = attachmentJson.value("hull", 0);
                                mesh.triangles = attachmentJson.value("triangles", std::vector<unsigned short>{});
                                mesh.edges = attachmentJson.value("edges", std::vector<unsigned short>{});
                                mesh.uvs = attachmentJson.value("uvs", std::vector<float>{});
                                mesh.vertices = attachmentJson.value("vertices", std::vector<float>{});
                                attachment.data = mesh;
                                break;
                            }
                            case AttachmentType_Boundingbox: {
                                BoundingboxAttachment boundingBox;
                                boundingBox.vertexCount = attachmentJson.value("vertexCount", 0);
                                if (attachmentJson.contains("color")) boundingBox.color = stringToColor(attachmentJson["color"], true);
                                boundingBox.vertices = attachmentJson.value("vertices", std::vector<float>{});
                                attachment.data = boundingBox;
                                break;
                            }
                            case AttachmentType_Path: {
                                PathAttachment path;
                                path.vertexCount = attachmentJson.value("vertexCount", 0);
                                path.closed = attachmentJson.value("closed", false);
                                path.constantSpeed = attachmentJson.value("constantSpeed", true);
                                if (attachmentJson.contains("color")) path.color = stringToColor(attachmentJson["color"], true);
                                path.vertices = attachmentJson.value("vertices", std::vector<float>{});
                                path.lengths = attachmentJson.value("lengths", std::vector<float>{});
                                attachment.data = path;
                                break;
                            }
                            case AttachmentType_Point: {
                                PointAttachment point;
                                point.x = attachmentJson.value("x", 0.0f);
                                point.y = attachmentJson.value("y", 0.0f);
                                point.rotation = attachmentJson.value("rotation", 0.0f);
                                if (attachmentJson.contains("color")) point.color = stringToColor(attachmentJson["color"], true);
                                attachment.data = point;
                                break;
                            }
                            case AttachmentType_Clipping: {
                                ClippingAttachment clipping;
                                clipping.vertexCount = attachmentJson.value("vertexCount", 0);
                                if (attachmentJson.contains("color")) clipping.color = stringToColor(attachmentJson["color"], true);
                                if (attachmentJson.contains("end")) clipping.endSlot = attachmentJson["end"];
                                clipping.vertices = attachmentJson.value("vertices", std::vector<float>{});
                                attachment.data = clipping;
                                break;
                            }
                            default:
                                continue;
                        }
                        skinData.attachments[slotName][attachmentName] = attachment;
                    }
                }
            }
            skeletonData.skins.push_back(skinData);
        }
    }

    /* Events */
    if (j.contains("events")) {
        for (const auto& [eventName, eventJson] : j["events"].items()) {
            EventData eventData;
            eventData.name = eventName;
            eventData.intValue = eventJson.value("int", 0);
            eventData.floatValue = eventJson.value("float", 0.0f);
            if (eventJson.contains("string")) eventData.stringValue = eventJson["string"];
            if (eventJson.contains("audio")) {
                eventData.audioPath = eventJson["audio"];
                eventData.volume = eventJson.value("volume", 1.0f);
                eventData.balance = eventJson.value("balance", 0.0f);
            }
            skeletonData.events.push_back(eventData);
        }
    }

    /* Animations (slider timelines and drawOrderFolder dropped) */
    if (j.contains("animations")) {
        for (const auto& [animationName, animationJson] : j["animations"].items()) {
            Animation animationData;
            animationData.name = animationName;
            if (animationJson.contains("slots")) {
                for (const auto& [slotName, slotJson] : animationJson["slots"].items()) {
                    MultiTimeline slotTimeline;
                    if (slotJson.contains("attachment")) {
                        for (const auto& frameJson : slotJson["attachment"]) {
                            TimelineFrame frame;
                            frame.time = frameJson.value("time", 0.0f);
                            if (frameJson.contains("name") && !frameJson["name"].is_null()) frame.str1 = frameJson["name"];
                            slotTimeline["attachment"].push_back(frame);
                        }
                    }
                    if (slotJson.contains("rgba")) {
                        for (const auto& frameJson : slotJson["rgba"]) {
                            TimelineFrame frame;
                            frame.time = frameJson.value("time", 0.0f);
                            if (frameJson.contains("color")) frame.color1 = stringToColor(frameJson["color"], true);
                            readCurve(frameJson, frame);
                            slotTimeline["rgba"].push_back(frame);
                        }
                    }
                    if (slotJson.contains("rgb")) {
                        for (const auto& frameJson : slotJson["rgb"]) {
                            TimelineFrame frame;
                            frame.time = frameJson.value("time", 0.0f);
                            if (frameJson.contains("color")) frame.color1 = stringToColor(frameJson["color"], false);
                            readCurve(frameJson, frame);
                            slotTimeline["rgb"].push_back(frame);
                        }
                    }
                    if (slotJson.contains("alpha")) {
                        readTimeline(slotJson["alpha"], slotTimeline["alpha"], 1, "value", "", 0.0f);
                    }
                    if (slotJson.contains("rgba2")) {
                        for (const auto& frameJson : slotJson["rgba2"]) {
                            TimelineFrame frame;
                            frame.time = frameJson.value("time", 0.0f);
                            if (frameJson.contains("light")) frame.color1 = stringToColor(frameJson["light"], true);
                            if (frameJson.contains("dark")) frame.color2 = stringToColor(frameJson["dark"], false);
                            readCurve(frameJson, frame);
                            slotTimeline["rgba2"].push_back(frame);
                        }
                    }
                    if (slotJson.contains("rgb2")) {
                        for (const auto& frameJson : slotJson["rgb2"]) {
                            TimelineFrame frame;
                            frame.time = frameJson.value("time", 0.0f);
                            if (frameJson.contains("light")) frame.color1 = stringToColor(frameJson["light"], false);
                            if (frameJson.contains("dark")) frame.color2 = stringToColor(frameJson["dark"], false);
                            readCurve(frameJson, frame);
                            slotTimeline["rgb2"].push_back(frame);
                        }
                    }
                    animationData.slots[slotName] = slotTimeline;
                }
            }
            if (animationJson.contains("bones")) {
                for (const auto& [boneName, boneJson] : animationJson["bones"].items()) {
                    MultiTimeline boneTimeline;
                    if (boneJson.contains("rotate")) {
                        readTimeline(boneJson["rotate"], boneTimeline["rotate"], 1, "value", "", 0.0f);
                    }
                    if (boneJson.contains("translate")) {
                        readTimeline(boneJson["translate"], boneTimeline["translate"], 2, "x", "y", 0.0f);
                    }
                    if (boneJson.contains("translatex")) {
                        readTimeline(boneJson["translatex"], boneTimeline["translatex"], 1, "value", "", 0.0f);
                    }
                    if (boneJson.contains("translatey")) {
                        readTimeline(boneJson["translatey"], boneTimeline["translatey"], 1, "value", "", 0.0f);
                    }
                    if (boneJson.contains("scale")) {
                        readTimeline(boneJson["scale"], boneTimeline["scale"], 2, "x", "y", 1.0f);
                    }
                    if (boneJson.contains("scalex")) {
                        readTimeline(boneJson["scalex"], boneTimeline["scalex"], 1, "value", "", 1.0f);
                    }
                    if (boneJson.contains("scaley")) {
                        readTimeline(boneJson["scaley"], boneTimeline["scaley"], 1, "value", "", 1.0f);
                    }
                    if (boneJson.contains("shear")) {
                        readTimeline(boneJson["shear"], boneTimeline["shear"], 2, "x", "y", 0.0f);
                    }
                    if (boneJson.contains("shearx")) {
                        readTimeline(boneJson["shearx"], boneTimeline["shearx"], 1, "value", "", 0.0f);
                    }
                    if (boneJson.contains("sheary")) {
                        readTimeline(boneJson["sheary"], boneTimeline["sheary"], 1, "value", "", 0.0f);
                    }
                    if (boneJson.contains("inherit")) {
                        for (const auto& frameJson : boneJson["inherit"]) {
                            TimelineFrame frame;
                            frame.time = frameJson.value("time", 0.0f);
                            frame.inherit = inheritMap.at(frameJson.value("inherit", "normal"));
                            boneTimeline["inherit"].push_back(frame);
                        }
                    }
                    animationData.bones[boneName] = boneTimeline;
                }
            }
            if (animationJson.contains("ik")) {
                for (const auto& [ikName, ikJson] : animationJson["ik"].items()) {
                    Timeline ikTimeline;
                    for (const auto& frameJson : ikJson) {
                        TimelineFrame frame;
                        frame.time = frameJson.value("time", 0.0f);
                        frame.value1 = frameJson.value("mix", 1.0f);
                        frame.value2 = frameJson.value("softness", 0.0f);
                        frame.bendPositive = frameJson.value("bendPositive", true);
                        frame.compress = frameJson.value("compress", false);
                        frame.stretch = frameJson.value("stretch", false);
                        readCurve(frameJson, frame);
                        ikTimeline.push_back(frame);
                    }
                    animationData.ik[ikName] = ikTimeline;
                }
            }
            if (animationJson.contains("transform")) {
                for (const auto& [transformName, transformJson] : animationJson["transform"].items()) {
                    Timeline transformTimeline;
                    for (const auto& frameJson : transformJson) {
                        TimelineFrame frame;
                        frame.time = frameJson.value("time", 0.0f);
                        frame.value1 = frameJson.value("mixRotate", 1.0f);
                        frame.value2 = frameJson.value("mixX", 1.0f);
                        frame.value3 = frameJson.value("mixY", frame.value2);
                        frame.value4 = frameJson.value("mixScaleX", 1.0f);
                        frame.value5 = frameJson.value("mixScaleY", 1.0f);
                        frame.value6 = frameJson.value("mixShearY", 1.0f);
                        readCurve(frameJson, frame);
                        transformTimeline.push_back(frame);
                    }
                    animationData.transform[transformName] = transformTimeline;
                }
            }
            if (animationJson.contains("path")) {
                for (const auto& [pathName, pathJson] : animationJson["path"].items()) {
                    MultiTimeline pathTimeline;
                    if (pathJson.contains("position")) {
                        readTimeline(pathJson["position"], pathTimeline["position"], 1, "value", "", 0.0f);
                    }
                    if (pathJson.contains("spacing")) {
                        readTimeline(pathJson["spacing"], pathTimeline["spacing"], 1, "value", "", 0.0f);
                    }
                    if (pathJson.contains("mix")) {
                        for (const auto& frameJson : pathJson["mix"]) {
                            TimelineFrame frame;
                            frame.time = frameJson.value("time", 0.0f);
                            frame.value1 = frameJson.value("mixRotate", 1.0f);
                            frame.value2 = frameJson.value("mixX", 1.0f);
                            frame.value3 = frameJson.value("mixY", frame.value2);
                            readCurve(frameJson, frame);
                            pathTimeline["mix"].push_back(frame);
                        }
                    }
                    animationData.path[pathName] = pathTimeline;
                }
            }
            if (animationJson.contains("physics")) {
                for (const auto& [physicsName, physicsJson] : animationJson["physics"].items()) {
                    MultiTimeline physicsTimeline;
                    if (physicsJson.contains("reset")) {
                        for (const auto& frameJson : physicsJson["reset"]) {
                            TimelineFrame frame;
                            frame.time = frameJson.value("time", 0.0f);
                            physicsTimeline["reset"].push_back(frame);
                        }
                    }
                    if (physicsJson.contains("inertia")) {
                        readTimeline(physicsJson["inertia"], physicsTimeline["inertia"], 1, "value", "", 0.0f);
                    }
                    if (physicsJson.contains("strength")) {
                        readTimeline(physicsJson["strength"], physicsTimeline["strength"], 1, "value", "", 0.0f);
                    }
                    if (physicsJson.contains("damping")) {
                        readTimeline(physicsJson["damping"], physicsTimeline["damping"], 1, "value", "", 0.0f);
                    }
                    if (physicsJson.contains("mass")) {
                        readTimeline(physicsJson["mass"], physicsTimeline["mass"], 1, "value", "", 0.0f);
                    }
                    if (physicsJson.contains("wind")) {
                        readTimeline(physicsJson["wind"], physicsTimeline["wind"], 1, "value", "", 0.0f);
                    }
                    if (physicsJson.contains("gravity")) {
                        readTimeline(physicsJson["gravity"], physicsTimeline["gravity"], 1, "value", "", 0.0f);
                    }
                    if (physicsJson.contains("mix")) {
                        readTimeline(physicsJson["mix"], physicsTimeline["mix"], 1, "value", "", 1.0f);
                    }
                    if (physicsName.empty()) {
                        // 4.3 empty name = timeline applies to all physics constraints;
                        // expand per constraint (4.2 requires a specific name).
                        // Constraints with their own timeline keep it (specific wins).
                        for (const auto& pc : skeletonData.physicsConstraints) {
                            if (!pc.name) continue;
                            if (animationData.physics.find(*pc.name) == animationData.physics.end()) {
                                animationData.physics[*pc.name] = physicsTimeline;
                            }
                        }
                    } else {
                        animationData.physics[physicsName] = physicsTimeline;
                    }
                }
            }
            if (animationJson.contains("attachments")) {
                for (const auto& [skinName, skinJson] : animationJson["attachments"].items()) {
                    for (const auto& [slotName, slotJson] : skinJson.items()) {
                        for (const auto& [attachmentName, attachmentJson] : slotJson.items()) {
                            MultiTimeline attachmentTimeline;
                            if (attachmentJson.contains("deform")) {
                                for (const auto& frameJson : attachmentJson["deform"]) {
                                    TimelineFrame frame;
                                    frame.time = frameJson.value("time", 0.0f);
                                    if (frameJson.contains("vertices")) {
                                        frame.int1 = frameJson.value("offset", 0);
                                        frame.vertices = frameJson["vertices"].get<std::vector<float>>();
                                    }
                                    readCurve(frameJson, frame);
                                    attachmentTimeline["deform"].push_back(frame);
                                }
                            }
                            if (attachmentJson.contains("sequence")) {
                                float lastDelay = 0.0f;
                                for (const auto& frameJson : attachmentJson["sequence"]) {
                                    TimelineFrame frame;
                                    frame.time = frameJson.value("time", 0.0f);
                                    frame.value1 = frameJson.value("delay", lastDelay);
                                    lastDelay = frame.value1;
                                    frame.int1 = frameJson.value("index", 0);
                                    frame.sequenceMode = sequenceModeMap.at(frameJson.value("mode", "hold"));
                                    attachmentTimeline["sequence"].push_back(frame);
                                }
                            }
                            animationData.attachments[skinName][slotName][attachmentName] = attachmentTimeline;
                        }
                    }
                }
            }
            if (animationJson.contains("drawOrder")) {
                for (const auto& frameJson : animationJson["drawOrder"]) {
                    TimelineFrame frame;
                    frame.time = frameJson.value("time", 0.0f);
                    if (frameJson.contains("offsets")) {
                        for (const auto& offsetJson : frameJson["offsets"]) {
                            frame.offsets.push_back({offsetJson["slot"], offsetJson.value("offset", 0)});
                        }
                    }
                    animationData.drawOrder.push_back(frame);
                }
            }
            if (animationJson.contains("events")) {
                for (const auto& frameJson : animationJson["events"]) {
                    TimelineFrame frame;
                    frame.time = frameJson.value("time", 0.0f);
                    if (frameJson.contains("name")) frame.str1 = frameJson["name"];
                    int eventIndex = -1;
                    for (size_t i = 0; i < skeletonData.events.size(); i++) {
                        if (skeletonData.events[i].name == frame.str1.value()) {
                            eventIndex = static_cast<int>(i);
                            break;
                        }
                    }
                    EventData eventData = skeletonData.events[eventIndex];
                    frame.int1 = frameJson.value("int", eventData.intValue);
                    frame.value1 = frameJson.value("float", eventData.floatValue);
                    if (frameJson.contains("string")) frame.str2 = frameJson["string"];
                    else frame.str2 = eventData.stringValue;
                    if (eventData.audioPath) {
                        frame.value2 = frameJson.value("volume", 1.0f);
                        frame.value3 = frameJson.value("balance", 0.0f);
                    }
                    animationData.events.push_back(frame);
                }
            }
            skeletonData.animations.push_back(animationData);
        }
    }

    return skeletonData;
}

}
