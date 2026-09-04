#include "SkeletonData.h"

#include <optional>

namespace spine43 {

// 4.3 unified constraint type tags (spine-cpp 4.3 SkeletonBinary.h)
static const int CONSTRAINT_IK_43 = 0;
static const int CONSTRAINT_PATH_43 = 1;
static const int CONSTRAINT_TRANSFORM_43 = 2;
static const int CONSTRAINT_PHYSICS_43 = 3;
static const int CONSTRAINT_SLIDER_43 = 4;

// Tracks the unified constraints array so skins/timelines can resolve
// unified indices to per-type constraint names; slider entries are dropped.
struct ConstraintRef {
    int kind = -1;
    std::string name = "";
};

Sequence readSequence(DataInput* input) {
    Sequence sequence;
    sequence.count = readVarint(input, true);
    sequence.start = readVarint(input, true);
    sequence.digits = readVarint(input, true);
    sequence.setupIndex = readVarint(input, true);
    return sequence;
}

void readFloatArray(DataInput* input, int n, std::vector<float>& array) {
    array.resize(n, 0);
    for (int i = 0; i < n; i++)
        array[i] = readFloat(input);
}

void readShortArray(DataInput* input, int n, std::vector<unsigned short>& array) {
    array.resize(n, 0);
    for (int i = 0; i < n; i++)
        array[i] = (short) readVarint(input, true);
}

int readVertices(DataInput* input, std::vector<float>& vertices, bool weighted) {
    int vertexCount = readVarint(input, true);
    if (!weighted) {
        readFloatArray(input, vertexCount << 1, vertices);
    } else {
        // 4.3 writes the total bones-array size before the weighted entries; 4.2 does not
        int n = readVarint(input, true);
        (void) n;
        for (int i = 0; i < vertexCount; i++) {
            int boneCount = readVarint(input, true);
            vertices.push_back(boneCount);
            for (int ii = 0; ii < boneCount; ii++) {
                vertices.push_back(readVarint(input, true));
                vertices.push_back(readFloat(input));
                vertices.push_back(readFloat(input));
                vertices.push_back(readFloat(input));
            }
        }
    }
    return vertexCount;
}

void readCurve(DataInput* input, TimelineFrame& frame, int timelineCount) {
    for (int i = 0; i < timelineCount * 4; i++) {
        frame.curve.push_back(readFloat(input));
    }
}

Timeline readTimeline(DataInput* input, int frameCount, int valueNum) {
    Timeline timeline;
    float time = readFloat(input);
    float value1 = readFloat(input);
    float value2 = valueNum > 1 ? readFloat(input) : 0.0f;
    float value3 = valueNum > 2 ? readFloat(input) : 0.0f;
    for (int frameIndex = 0; frameIndex < frameCount - 1; frameIndex++) {
        TimelineFrame frame;
        frame.time = time;
        frame.value1 = value1;
        if (valueNum > 1) frame.value2 = value2;
        if (valueNum > 2) frame.value3 = value3;
        time = readFloat(input);
        value1 = readFloat(input);
        if (valueNum > 1) value2 = readFloat(input);
        if (valueNum > 2) value3 = readFloat(input);
        switch (readSByte(input)) {
            case CURVE_STEPPED: {
                frame.curveType = CurveType::CURVE_STEPPED;
                break;
            }
            case CURVE_BEZIER: {
                frame.curveType = CurveType::CURVE_BEZIER;
                readCurve(input, frame, valueNum);
                break;
            }
        }
        timeline.push_back(frame);
    }
    TimelineFrame frame;
    frame.time = time;
    frame.value1 = value1;
    if (valueNum > 1) frame.value2 = value2;
    if (valueNum > 2) frame.value3 = value3;
    timeline.push_back(frame);
    return timeline;
}

// 4.3 default skin with zero slots is absent from the skins array (and shifts
// every later skin index), so return nullopt in that case — unlike 4.2.
std::optional<Skin> readSkin(DataInput* input, bool defaultSkin, SkeletonData* skeletonData,
                             const std::vector<ConstraintRef>& constraintRefs) {
    Skin skin;
    int slotCount = 0;
    if (defaultSkin) {
        slotCount = readVarint(input, true);
        if (slotCount == 0) return std::nullopt;
        skin.name = "default";
    } else {
        skin.name = readString(input).value();
        if (skeletonData->nonessential) {
            readColor(input); // skin color dropped in 4.2 downgrade
        }
        for (int i = 0, n = readVarint(input, true); i < n; i++) {
            skin.bones.push_back(skeletonData->bones[readVarint(input, true)].name.value());
        }
        for (int i = 0, n = readVarint(input, true); i < n; i++) {
            const ConstraintRef& ref = constraintRefs[readVarint(input, true)];
            if (ref.kind == CONSTRAINT_IK_43) skin.ik.push_back(ref.name);
            else if (ref.kind == CONSTRAINT_TRANSFORM_43) skin.transform.push_back(ref.name);
            else if (ref.kind == CONSTRAINT_PATH_43) skin.path.push_back(ref.name);
            else if (ref.kind == CONSTRAINT_PHYSICS_43) skin.physics.push_back(ref.name);
            // slider refs dropped
        }
        slotCount = readVarint(input, true);
    }
    for (int i = 0; i < slotCount; i++) {
        int slotIndex = readVarint(input, true);
        std::string slotName = skeletonData->slots[slotIndex].name.value();
        for (int ii = 0, nn = readVarint(input, true); ii < nn; ii++) {
            std::string attachmentName = readStringRef(input, skeletonData).value();
            Attachment attachment;
            int flags = readByte(input);
            attachment.name = (flags & 8) != 0 ? readStringRef(input, skeletonData).value() : attachmentName;
            attachment.type = static_cast<AttachmentType>(flags & 0x7);
            bool keep = true;
            switch (attachment.type) {
                case AttachmentType_Region: {
                    RegionAttachment region;
                    attachment.path = (flags & 16) != 0 ? readStringRef(input, skeletonData).value() : attachment.name;
                    if ((flags & 32) != 0) region.color = readColor(input);
                    if ((flags & 64) != 0) region.sequence = readSequence(input);
                    if ((flags & 128) != 0) region.rotation = readFloat(input);
                    region.x = readFloat(input);
                    region.y = readFloat(input);
                    region.scaleX = readFloat(input);
                    region.scaleY = readFloat(input);
                    region.width = readFloat(input);
                    region.height = readFloat(input);
                    attachment.data = region;
                    break;
                }
                case AttachmentType_Boundingbox: {
                    BoundingboxAttachment box;
                    attachment.path = attachment.name;
                    int vertexCount = readVertices(input, box.vertices, (flags & 16) != 0);
                    box.vertexCount = vertexCount;
                    if (skeletonData->nonessential) {
                        Color color = readColor(input);
                        if (color != Color{0xff, 0xff, 0xff, 0xff}) box.color = color;
                    }
                    attachment.data = box;
                    break;
                }
                case AttachmentType_Mesh: {
                    MeshAttachment mesh;
                    attachment.path = (flags & 16) != 0 ? readStringRef(input, skeletonData).value() : attachment.name;
                    if ((flags & 32) != 0) mesh.color = readColor(input);
                    if ((flags & 64) != 0) mesh.sequence = readSequence(input);
                    mesh.hullLength = readVarint(input, true);
                    int vertexCount = readVertices(input, mesh.vertices, (flags & 128) != 0);
                    readFloatArray(input, vertexCount << 1, mesh.uvs);
                    readShortArray(input, (vertexCount * 2 - mesh.hullLength - 2) * 3, mesh.triangles);
                    // 4.3 timeline slots are not representable in 4.2 — consume and drop
                    int timelineSlotCount = readVarint(input, true);
                    for (int ts = 0; ts < timelineSlotCount; ts++)
                        readVarint(input, true);
                    if (skeletonData->nonessential) {
                        readShortArray(input, readVarint(input, true), mesh.edges);
                        mesh.width = readFloat(input);
                        mesh.height = readFloat(input);
                    }
                    attachment.data = mesh;
                    break;
                }
                case AttachmentType_Linkedmesh: {
                    LinkedmeshAttachment linkedMesh;
                    attachment.path = (flags & 16) != 0 ? readStringRef(input, skeletonData).value() : attachment.name;
                    if ((flags & 32) != 0) linkedMesh.color = readColor(input);
                    if ((flags & 64) != 0) linkedMesh.sequence = readSequence(input);
                    linkedMesh.timelines = (flags & 128) != 0 ? 1 : 0;
                    int sourceSlotIndex = readVarint(input, true);
                    linkedMesh.skinIndex = readVarint(input, true);
                    linkedMesh.parentMesh = readStringRef(input, skeletonData).value();
                    if (skeletonData->nonessential) {
                        linkedMesh.width = readFloat(input);
                        linkedMesh.height = readFloat(input);
                    }
                    attachment.data = linkedMesh;
                    // 4.3 linked meshes may reference a parent in another slot; 4.2 cannot express that
                    if (sourceSlotIndex != slotIndex) keep = false;
                    break;
                }
                case AttachmentType_Path: {
                    PathAttachment path;
                    attachment.path = attachment.name;
                    path.closed = (flags & 16) != 0;
                    path.constantSpeed = (flags & 32) != 0;
                    int vertexCount = readVertices(input, path.vertices, (flags & 64) != 0);
                    path.vertexCount = vertexCount;
                    readFloatArray(input, vertexCount / 3, path.lengths);
                    if (skeletonData->nonessential) {
                        Color color = readColor(input);
                        if (color != Color{0xff, 0xff, 0xff, 0xff}) path.color = color;
                    }
                    attachment.data = path;
                    break;
                }
                case AttachmentType_Point: {
                    // 4.3 order is rotation, x, y (4.2 was x, y, rotation)
                    PointAttachment point;
                    attachment.path = attachment.name;
                    point.rotation = readFloat(input);
                    point.x = readFloat(input);
                    point.y = readFloat(input);
                    if (skeletonData->nonessential) {
                        Color color = readColor(input);
                        if (color != Color{0xff, 0xff, 0xff, 0xff}) point.color = color;
                    }
                    attachment.data = point;
                    break;
                }
                case AttachmentType_Clipping: {
                    ClippingAttachment clipping;
                    attachment.path = attachment.name;
                    clipping.endSlot = skeletonData->slots[readVarint(input, true)].name;
                    int vertexCount = readVertices(input, clipping.vertices, (flags & 16) != 0);
                    clipping.vertexCount = vertexCount;
                    // convex (bit 32) / inverse (bit 64) dropped in 4.2 downgrade
                    if (skeletonData->nonessential) {
                        Color color = readColor(input);
                        if (color != Color{0xff, 0xff, 0xff, 0xff}) clipping.color = color;
                    }
                    attachment.data = clipping;
                    break;
                }
            }
            if (keep)
                skin.attachments[slotName][attachmentName] = attachment;
        }
    }
    return skin;
}

Animation readAnimation(DataInput* input, SkeletonData* skeletonData,
                        const std::vector<ConstraintRef>& constraintRefs) {
    Animation animation;
    animation.name = readString(input).value();
    int numTimelines = readVarint(input, true);
    (void) numTimelines;
    for (int i = 0, n = readVarint(input, true); i < n; i++) {
        std::string slotName = skeletonData->slots[readVarint(input, true)].name.value();
        MultiTimeline slotTimeline;
        for (int ii = 0, nn = readVarint(input, true); ii < nn; ii++) {
            SlotTimelineType timelineType = static_cast<SlotTimelineType>(readByte(input));
            int frameCount = readVarint(input, true);
            switch (timelineType) {
                case SlotTimelineType::SLOT_ATTACHMENT: {
                    Timeline timeline;
                    for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
                        TimelineFrame frame;
                        frame.time = readFloat(input);
                        frame.str1 = readStringRef(input, skeletonData);
                        timeline.push_back(frame);
                    }
                    slotTimeline["attachment"] = timeline;
                    break;
                }
                case SlotTimelineType::SLOT_RGBA: {
                    Timeline timeline;
                    int bezierCount = readVarint(input, true);
                    (void) bezierCount;
                    float time = readFloat(input);
                    Color color = readColor(input);
                    for (int frameIndex = 0; frameIndex < frameCount - 1; frameIndex++) {
                        TimelineFrame frame;
                        frame.time = time;
                        frame.color1 = color;
                        time = readFloat(input);
                        color = readColor(input);
                        switch (readSByte(input)) {
                            case CURVE_STEPPED: {
                                frame.curveType = CurveType::CURVE_STEPPED;
                                break;
                            }
                            case CURVE_BEZIER: {
                                frame.curveType = CurveType::CURVE_BEZIER;
                                readCurve(input, frame, 4);
                                break;
                            }
                        }
                        timeline.push_back(frame);
                    }
                    TimelineFrame frame;
                    frame.time = time;
                    frame.color1 = color;
                    timeline.push_back(frame);
                    slotTimeline["rgba"] = timeline;
                    break;
                }
                case SlotTimelineType::SLOT_RGB: {
                    Timeline timeline;
                    int bezierCount = readVarint(input, true);
                    (void) bezierCount;
                    float time = readFloat(input);
                    Color color = readColor(input, false);
                    for (int frameIndex = 0; frameIndex < frameCount - 1; frameIndex++) {
                        TimelineFrame frame;
                        frame.time = time;
                        frame.color1 = color;
                        time = readFloat(input);
                        color = readColor(input, false);
                        switch (readSByte(input)) {
                            case CURVE_STEPPED: {
                                frame.curveType = CurveType::CURVE_STEPPED;
                                break;
                            }
                            case CURVE_BEZIER: {
                                frame.curveType = CurveType::CURVE_BEZIER;
                                readCurve(input, frame, 3);
                                break;
                            }
                        }
                        timeline.push_back(frame);
                    }
                    TimelineFrame frame;
                    frame.time = time;
                    frame.color1 = color;
                    timeline.push_back(frame);
                    slotTimeline["rgb"] = timeline;
                    break;
                }
                case SlotTimelineType::SLOT_RGBA2: {
                    Timeline timeline;
                    int bezierCount = readVarint(input, true);
                    (void) bezierCount;
                    float time = readFloat(input);
                    Color light = readColor(input);
                    Color dark = readColor(input, false);
                    for (int frameIndex = 0; frameIndex < frameCount - 1; frameIndex++) {
                        TimelineFrame frame;
                        frame.time = time;
                        frame.color1 = light;
                        frame.color2 = dark;
                        time = readFloat(input);
                        light = readColor(input);
                        dark = readColor(input, false);
                        switch (readSByte(input)) {
                            case CURVE_STEPPED: {
                                frame.curveType = CurveType::CURVE_STEPPED;
                                break;
                            }
                            case CURVE_BEZIER: {
                                frame.curveType = CurveType::CURVE_BEZIER;
                                readCurve(input, frame, 7);
                                break;
                            }
                        }
                        timeline.push_back(frame);
                    }
                    TimelineFrame frame;
                    frame.time = time;
                    frame.color1 = light;
                    frame.color2 = dark;
                    timeline.push_back(frame);
                    slotTimeline["rgba2"] = timeline;
                    break;
                }
                case SlotTimelineType::SLOT_RGB2: {
                    Timeline timeline;
                    int bezierCount = readVarint(input, true);
                    (void) bezierCount;
                    float time = readFloat(input);
                    Color light = readColor(input, false);
                    Color dark = readColor(input, false);
                    for (int frameIndex = 0; frameIndex < frameCount - 1; frameIndex++) {
                        TimelineFrame frame;
                        frame.time = time;
                        frame.color1 = light;
                        frame.color2 = dark;
                        time = readFloat(input);
                        light = readColor(input, false);
                        dark = readColor(input, false);
                        switch (readSByte(input)) {
                            case CURVE_STEPPED: {
                                frame.curveType = CurveType::CURVE_STEPPED;
                                break;
                            }
                            case CURVE_BEZIER: {
                                frame.curveType = CurveType::CURVE_BEZIER;
                                readCurve(input, frame, 6);
                                break;
                            }
                        }
                        timeline.push_back(frame);
                    }
                    TimelineFrame frame;
                    frame.time = time;
                    frame.color1 = light;
                    frame.color2 = dark;
                    timeline.push_back(frame);
                    slotTimeline["rgb2"] = timeline;
                    break;
                }
                case SlotTimelineType::SLOT_ALPHA: {
                    Timeline timeline;
                    int bezierCount = readVarint(input, true);
                    (void) bezierCount;
                    float time = readFloat(input);
                    float alpha = readByte(input) / 255.0f;
                    for (int frameIndex = 0; ; frameIndex++) {
                        TimelineFrame frame;
                        frame.time = time;
                        frame.value1 = alpha;
                        if (frameIndex == frameCount - 1) {
                            timeline.push_back(frame);
                            break;
                        }
                        time = readFloat(input);
                        alpha = readByte(input) / 255.0f;
                        switch (readSByte(input)) {
                            case CURVE_STEPPED: {
                                frame.curveType = CurveType::CURVE_STEPPED;
                                break;
                            }
                            case CURVE_BEZIER: {
                                frame.curveType = CurveType::CURVE_BEZIER;
                                readCurve(input, frame, 1);
                                break;
                            }
                        }
                        timeline.push_back(frame);
                    }
                    slotTimeline["alpha"] = timeline;
                    break;
                }
            }
        }
        animation.slots[slotName] = slotTimeline;
    }
    for (int i = 0, n = readVarint(input, true); i < n; i++) {
        std::string boneName = skeletonData->bones[readVarint(input, true)].name.value();
        MultiTimeline boneTimeline;
        for (int ii = 0, nn = readVarint(input, true); ii < nn; ii++) {
            BoneTimelineType timelineType = static_cast<BoneTimelineType>(readByte(input));
            int frameCount = readVarint(input, true);
            if (timelineType == BONE_INHERIT) {
                Timeline timeline;
                for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
                    TimelineFrame frame;
                    frame.time = readFloat(input);
                    frame.inherit = (Inherit) readByte(input);
                    timeline.push_back(frame);
                }
                boneTimeline["inherit"] = timeline;
                continue;
            }
            int bezierCount = readVarint(input, true);
            (void) bezierCount;
            switch (timelineType) {
                case BONE_ROTATE: {
                    boneTimeline["rotate"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case BONE_TRANSLATE: {
                    boneTimeline["translate"] = readTimeline(input, frameCount, 2);
                    break;
                }
                case BONE_TRANSLATEX: {
                    boneTimeline["translatex"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case BONE_TRANSLATEY: {
                    boneTimeline["translatey"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case BONE_SCALE: {
                    boneTimeline["scale"] = readTimeline(input, frameCount, 2);
                    break;
                }
                case BONE_SCALEX: {
                    boneTimeline["scalex"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case BONE_SCALEY: {
                    boneTimeline["scaley"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case BONE_SHEAR: {
                    boneTimeline["shear"] = readTimeline(input, frameCount, 2);
                    break;
                }
                case BONE_SHEARX: {
                    boneTimeline["shearx"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case BONE_SHEARY: {
                    boneTimeline["sheary"] = readTimeline(input, frameCount, 1);
                    break;
                }
            }
        }
        animation.bones[boneName] = boneTimeline;
    }
    // 4.3 constraint timelines index the unified constraints array
    for (int i = 0, n = readVarint(input, true); i < n; i++) {
        int index = readVarint(input, true);
        int frameCount = readVarint(input, true);
        int bezierCount = readVarint(input, true);
        (void) bezierCount;
        Timeline timeline;
        int flags = readByte(input);
        float time = readFloat(input);
        float mix = (flags & 1) != 0 ? ((flags & 2) != 0 ? readFloat(input) : 1) : 0;
        float softness = (flags & 4) != 0 ? readFloat(input) : 0;
        bool bendPositive = (flags & 8) != 0;
        bool compress = (flags & 16) != 0;
        bool stretch = (flags & 32) != 0;
        for (int frameIndex = 0; frameIndex < frameCount - 1; frameIndex++) {
            TimelineFrame frame;
            frame.time = time;
            frame.value1 = mix;
            frame.value2 = softness;
            frame.bendPositive = bendPositive;
            frame.compress = compress;
            frame.stretch = stretch;
            flags = readByte(input);
            time = readFloat(input);
            mix = (flags & 1) != 0 ? (flags & 2) != 0 ? readFloat(input) : 1 : 0;
            softness = (flags & 4) != 0 ? readFloat(input) : 0;
            bendPositive = (flags & 8) != 0;
            compress = (flags & 16) != 0;
            stretch = (flags & 32) != 0;
            if ((flags & 64) != 0) {
                frame.curveType = CurveType::CURVE_STEPPED;
            } else if ((flags & 128) != 0) {
                frame.curveType = CurveType::CURVE_BEZIER;
                readCurve(input, frame, 2);
            }
            timeline.push_back(frame);
        }
        TimelineFrame frame;
        frame.time = time;
        frame.value1 = mix;
        frame.value2 = softness;
        frame.bendPositive = bendPositive;
        frame.compress = compress;
        frame.stretch = stretch;
        timeline.push_back(frame);
        if (index >= 0 && index < (int) constraintRefs.size() && constraintRefs[index].kind == CONSTRAINT_IK_43)
            animation.ik[constraintRefs[index].name] = timeline;
    }
    for (int i = 0, n = readVarint(input, true); i < n; ++i) {
        int index = readVarint(input, true);
        int frameCount = readVarint(input, true);
        int bezierCount = readVarint(input, true);
        (void) bezierCount;
        Timeline timeline;
        float time = readFloat(input);
        float mixRotate = readFloat(input);
        float mixX = readFloat(input);
        float mixY = readFloat(input);
        float mixScaleX = readFloat(input);
        float mixScaleY = readFloat(input);
        float mixShearY = readFloat(input);
        for (int frameIndex = 0; frameIndex < frameCount - 1; frameIndex++) {
            TimelineFrame frame;
            frame.time = time;
            frame.value1 = mixRotate;
            frame.value2 = mixX;
            frame.value3 = mixY;
            frame.value4 = mixScaleX;
            frame.value5 = mixScaleY;
            frame.value6 = mixShearY;
            time = readFloat(input);
            mixRotate = readFloat(input);
            mixX = readFloat(input);
            mixY = readFloat(input);
            mixScaleX = readFloat(input);
            mixScaleY = readFloat(input);
            mixShearY = readFloat(input);
            switch (readSByte(input)) {
                case CURVE_STEPPED: {
                    frame.curveType = CurveType::CURVE_STEPPED;
                    break;
                }
                case CURVE_BEZIER: {
                    frame.curveType = CurveType::CURVE_BEZIER;
                    readCurve(input, frame, 6);
                    break;
                }
            }
            timeline.push_back(frame);
        }
        TimelineFrame frame;
        frame.time = time;
        frame.value1 = mixRotate;
        frame.value2 = mixX;
        frame.value3 = mixY;
        frame.value4 = mixScaleX;
        frame.value5 = mixScaleY;
        frame.value6 = mixShearY;
        timeline.push_back(frame);
        if (index >= 0 && index < (int) constraintRefs.size() && constraintRefs[index].kind == CONSTRAINT_TRANSFORM_43)
            animation.transform[constraintRefs[index].name] = timeline;
    }
    for (int i = 0, n = readVarint(input, true); i < n; i++) {
        int index = readVarint(input, true);
        MultiTimeline pathTimeline;
        for (int ii = 0, nn = readVarint(input, true); ii < nn; ii++) {
            PathTimelineType timelineType = static_cast<PathTimelineType>(readByte(input));
            int frameCount = readVarint(input, true);
            int bezierCount = readVarint(input, true);
            (void) bezierCount;
            switch (timelineType) {
                case PATH_POSITION: {
                    pathTimeline["position"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case PATH_SPACING: {
                    pathTimeline["spacing"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case PATH_MIX: {
                    pathTimeline["mix"] = readTimeline(input, frameCount, 3);
                    break;
                }
            }
        }
        if (index >= 0 && index < (int) constraintRefs.size() && constraintRefs[index].kind == CONSTRAINT_PATH_43)
            animation.path[constraintRefs[index].name] = pathTimeline;
    }
    for (int i = 0, n = readVarint(input, true); i < n; i++) {
        int index = readVarint(input, true) - 1;
        MultiTimeline physicsTimeline;
        for (int ii = 0, nn = readVarint(input, true); ii < nn; ii++) {
            PhysicsTimelineType timelineType = static_cast<PhysicsTimelineType>(readByte(input));
            int frameCount = readVarint(input, true);
            if (timelineType == PHYSICS_RESET) {
                Timeline timeline;
                for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
                    TimelineFrame frame;
                    frame.time = readFloat(input);
                    timeline.push_back(frame);
                }
                physicsTimeline["reset"] = timeline;
                continue;
            }
            int bezierCount = readVarint(input, true);
            (void) bezierCount;
            switch (timelineType) {
                case PHYSICS_INERTIA: {
                    physicsTimeline["inertia"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case PHYSICS_STRENGTH: {
                    physicsTimeline["strength"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case PHYSICS_DAMPING: {
                    physicsTimeline["damping"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case PHYSICS_MASS: {
                    physicsTimeline["mass"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case PHYSICS_WIND: {
                    physicsTimeline["wind"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case PHYSICS_GRAVITY: {
                    physicsTimeline["gravity"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case PHYSICS_MIX: {
                    physicsTimeline["mix"] = readTimeline(input, frameCount, 1);
                    break;
                }
            }
        }
        if (index == -1) {
            // 4.3: index 0 (stored -1) = timeline applies to all physics constraints
            animation.physics[""] = physicsTimeline;
        } else if (index < (int) constraintRefs.size() && constraintRefs[index].kind == CONSTRAINT_PHYSICS_43) {
            animation.physics[constraintRefs[index].name] = physicsTimeline;
        }
    }
    // Expand the all-physics timeline to every constraint without its own timeline
    auto allIt = animation.physics.find("");
    if (allIt != animation.physics.end()) {
        MultiTimeline all = allIt->second;
        animation.physics.erase(allIt);
        for (const auto& pc : skeletonData->physicsConstraints) {
            if (!pc.name) continue;
            if (animation.physics.count(*pc.name)) continue;
            animation.physics[*pc.name] = all;
        }
    }
    // Slider timelines: consume and drop
    for (int i = 0, n = readVarint(input, true); i < n; i++) {
        readVarint(input, true); // constraint index
        for (int ii = 0, nn = readVarint(input, true); ii < nn; ii++) {
            readByte(input); // timeline type
            int frameCount = readVarint(input, true);
            int bezierCount = readVarint(input, true);
            (void) bezierCount;
            readTimeline(input, frameCount, 1);
        }
    }
    for (int i = 0, n = readVarint(input, true); i < n; ++i) {
        std::string skinName = skeletonData->skins[readVarint(input, true)].name;
        for (int ii = 0, nn = readVarint(input, true); ii < nn; ++ii) {
            std::string slotName = skeletonData->slots[readVarint(input, true)].name.value();
            for (int iii = 0, nnn = readVarint(input, true); iii < nnn; iii++) {
                std::string attachmentName = readStringRef(input, skeletonData).value();
                MultiTimeline attachmentTimeline;
                AttachmentTimelineType timelineType = static_cast<AttachmentTimelineType>(readByte(input));
                int frameCount = readVarint(input, true);
                switch (timelineType) {
                    case ATTACHMENT_DEFORM: {
                        int bezierCount = readVarint(input, true);
                        (void) bezierCount;
                        float time = readFloat(input);
                        for (int frameIndex = 0; ; frameIndex++) {
                            TimelineFrame frame;
                            frame.time = time;
                            size_t end = (size_t) readVarint(input, true);
                            if (end != 0) {
                                size_t start = (size_t) readVarint(input, true);
                                frame.int1 = start;
                                end += start;
                                for (size_t v = start; v < end; v++)
                                    frame.vertices.push_back(readFloat(input));
                            }
                            if (frameIndex == frameCount - 1) {
                                attachmentTimeline["deform"].push_back(frame);
                                break;
                            }
                            time = readFloat(input);
                            switch (readSByte(input)) {
                                case CURVE_STEPPED: {
                                    frame.curveType = CurveType::CURVE_STEPPED;
                                    break;
                                }
                                case CURVE_BEZIER: {
                                    frame.curveType = CurveType::CURVE_BEZIER;
                                    readCurve(input, frame, 1);
                                    break;
                                }
                            }
                            attachmentTimeline["deform"].push_back(frame);
                        }
                        break;
                    }
                    case ATTACHMENT_SEQUENCE: {
                        for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
                            TimelineFrame frame;
                            frame.time = readFloat(input);
                            int modeAndIndex = readInt(input);
                            frame.sequenceMode = (SequenceMode) (modeAndIndex & 0xf);
                            frame.int1 = modeAndIndex >> 4;
                            frame.value1 = readFloat(input);
                            attachmentTimeline["sequence"].push_back(frame);
                        }
                        break;
                    }
                }
                animation.attachments[skinName][slotName][attachmentName] = attachmentTimeline;
            }
        }
    }
    size_t drawOrderCount = (size_t) readVarint(input, true);
    for (size_t i = 0; i < drawOrderCount; i++) {
        TimelineFrame frame;
        frame.time = readFloat(input);
        size_t offsetCount = (size_t) readVarint(input, true);
        for (size_t ii = 0; ii < offsetCount; ii++) {
            frame.offsets.push_back({
                skeletonData->slots[readVarint(input, true)].name.value(),
                readVarint(input, true)
            });
        }
        animation.drawOrder.push_back(frame);
    }
    // Draw order folder timelines: 4.3 only — consume and drop
    size_t folderCount = (size_t) readVarint(input, true);
    for (size_t i = 0; i < folderCount; i++) {
        size_t folderSlotCount = (size_t) readVarint(input, true);
        for (size_t ii = 0; ii < folderSlotCount; ii++) readVarint(input, true);
        size_t keyCount = (size_t) readVarint(input, true);
        for (size_t ii = 0; ii < keyCount; ii++) {
            readFloat(input);
            size_t changeCount = (size_t) readVarint(input, true);
            for (size_t iii = 0; iii < changeCount; iii++) {
                readVarint(input, true);
                readVarint(input, true);
            }
        }
    }
    int eventCount = readVarint(input, true);
    for (int i = 0; i < eventCount; i++) {
        TimelineFrame frame;
        frame.time = readFloat(input);
        int eventIndex = readVarint(input, true);
        const EventData& eventData = skeletonData->events[eventIndex];
        frame.str1 = eventData.name;
        frame.int1 = readVarint(input, false);
        frame.value1 = readFloat(input);
        OptStr str = readString(input);
        if (str.has_value()) frame.str2 = str.value();
        else frame.str2 = eventData.stringValue;
        if (eventData.audioPath) {
            frame.value2 = readFloat(input);
            frame.value3 = readFloat(input);
        }
        animation.events.push_back(frame);
    }
    if (skeletonData->nonessential) readInt(input); // animation color dropped
    return animation;
}

SkeletonData readBinaryData(const Binary& binary) {
    SkeletonData skeletonData;
    DataInput input;
    input.cursor = binary.data();
    input.end = binary.data() + binary.size();

    // 4.3 readLong: first int is the high 32 bits (matches base64 byte order in JSON exports)
    // Cast through uint32_t: readInt returns signed int and values >= 0x80000000 would
    // sign-extend and corrupt the other half when OR-ed together.
    uint64_t hashHigh = (uint64_t) (uint32_t) readInt(&input);
    uint64_t hashLow = (uint64_t) (uint32_t) readInt(&input);
    skeletonData.hash = (hashHigh << 32) | hashLow;
    skeletonData.version = readString(&input).value();

    skeletonData.x = readFloat(&input);
    skeletonData.y = readFloat(&input);
    skeletonData.width = readFloat(&input);
    skeletonData.height = readFloat(&input);
    skeletonData.referenceScale = readFloat(&input);

    skeletonData.nonessential = readBoolean(&input);

    if (skeletonData.nonessential) {
        skeletonData.fps = readFloat(&input);
        skeletonData.imagesPath = readString(&input);
        skeletonData.audioPath = readString(&input);
    }

    int numStrings = readVarint(&input, true);
    for (int i = 0; i < numStrings; i++)
        skeletonData.strings.push_back(readString(&input).value());

    /* Bones — 4.3 reads inherit (byte) before length; iconSize/iconRotation are new */
    int numBones = readVarint(&input, true);
    for (int i = 0; i < numBones; i++) {
        BoneData boneData;
        boneData.name = readString(&input);
        if (i != 0) boneData.parent = skeletonData.bones[readVarint(&input, true)].name;
        boneData.rotation = readFloat(&input);
        boneData.x = readFloat(&input);
        boneData.y = readFloat(&input);
        boneData.scaleX = readFloat(&input);
        boneData.scaleY = readFloat(&input);
        boneData.shearX = readFloat(&input);
        boneData.shearY = readFloat(&input);
        boneData.inherit = static_cast<Inherit>(readByte(&input));
        boneData.length = readFloat(&input);
        boneData.skinRequired = readBoolean(&input);
        if (skeletonData.nonessential) {
            Color color = readColor(&input);
            if (color != Color{0x9b, 0x9b, 0x9b, 0xff}) boneData.color = color;
            boneData.icon = readString(&input);
            readFloat(&input); // iconSize dropped
            readFloat(&input); // iconRotation dropped
            boneData.visible = readBoolean(&input);
        }
        skeletonData.bones.push_back(boneData);
    }

    /* Slots */
    int slotCount = readVarint(&input, true);
    for (int i = 0; i < slotCount; i++) {
        SlotData slotData;
        slotData.name = readString(&input);
        slotData.bone = skeletonData.bones[readVarint(&input, true)].name;
        Color color = readColor(&input);
        if (color != Color{0xff, 0xff, 0xff, 0xff}) slotData.color = color;
        unsigned char a = readByte(&input);
        unsigned char r = readByte(&input);
        unsigned char g = readByte(&input);
        unsigned char b = readByte(&input);
        if (!(r == 0xff && g == 0xff && b == 0xff && a == 0xff)) {
            slotData.darkColor = Color{ r, g, b, a };
        }
        slotData.attachmentName = readStringRef(&input, &skeletonData);
        slotData.blendMode = static_cast<BlendMode>(readVarint(&input, true));
        if (skeletonData.nonessential) slotData.visible = readBoolean(&input);
        skeletonData.slots.push_back(slotData);
    }

    /* Constraints — single unified array; order = array index */
    int constraintCount = readVarint(&input, true);
    std::vector<ConstraintRef> constraintRefs;
    constraintRefs.reserve(constraintCount);
    for (int i = 0; i < constraintCount; i++) {
        std::string name = readString(&input).value();
        int type = readByte(&input);
        ConstraintRef ref;
        ref.kind = type;
        ref.name = name;
        switch (type) {
            case CONSTRAINT_IK_43: {
                IKConstraintData ikData;
                ikData.name = name;
                ikData.order = constraintRefs.size();
                int bonesCount = readVarint(&input, true);
                for (int ii = 0; ii < bonesCount; ii++)
                    ikData.bones.push_back(skeletonData.bones[readVarint(&input, true)].name.value());
                ikData.target = skeletonData.bones[readVarint(&input, true)].name;
                int flags = readByte(&input);
                ikData.skinRequired = (flags & 1) != 0;
                if ((flags & 2) != 0) {
                    // scaleY mode: 0=none, 1=uniform, 2=volume (volume is lossy as bool)
                    int scaleYMode = readByte(&input);
                    ikData.uniform = scaleYMode != 0;
                }
                ikData.bendPositive = (flags & 4) == 0;
                ikData.compress = (flags & 8) != 0;
                ikData.stretch = (flags & 16) != 0;
                ikData.mix = (flags & 32) != 0 ? ((flags & 64) != 0 ? readFloat(&input) : 1.0f) : 0.0f;
                if ((flags & 128) != 0) ikData.softness = readFloat(&input);
                skeletonData.ikConstraints.push_back(ikData);
                break;
            }
            case CONSTRAINT_TRANSFORM_43: {
                TransformConstraintData transformData;
                transformData.name = name;
                transformData.order = constraintRefs.size();
                int bonesCount = readVarint(&input, true);
                for (int ii = 0; ii < bonesCount; ii++)
                    transformData.bones.push_back(skeletonData.bones[readVarint(&input, true)].name.value());
                transformData.target = skeletonData.bones[readVarint(&input, true)].name; // source bone
                int flags = readByte(&input);
                transformData.skinRequired = (flags & 1) != 0;
                transformData.relative = (flags & 2) != 0; // localSource
                transformData.local = (flags & 4) != 0;    // localTarget
                // additive (bit 3) / clamp (bit 4) dropped
                static const int propRotate = 0, propX = 1, propY = 2, propScaleX = 3, propScaleY = 4, propShearY = 5;
                bool enabled[6] = { false, false, false, false, false, false };
                float gain[6] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
                float fromOffset[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
                int propertyCount = flags >> 5;
                for (int p = 0; p < propertyCount; p++) {
                    int fromType = readByte(&input);
                    float fOffset = readFloat(&input);
                    int toCount = readByte(&input);
                    for (int t = 0; t < toCount; t++) {
                        int toType = readByte(&input);
                        readFloat(&input); // to offset dropped
                        readFloat(&input); // to max dropped
                        float toScale = readFloat(&input);
                        // 4.2 can only express a same-property pair keeping offset+mix
                        if (toType == fromType) {
                            enabled[fromType] = true;
                            gain[fromType] = toScale;
                        }
                    }
                    if (fromType >= 0 && fromType < 6) fromOffset[fromType] = fOffset;
                }
                flags = readByte(&input);
                transformData.offsetRotation = ((flags & 1) != 0 ? readFloat(&input) : 0.0f) + fromOffset[propRotate];
                transformData.offsetX = ((flags & 2) != 0 ? readFloat(&input) : 0.0f) + fromOffset[propX];
                transformData.offsetY = ((flags & 4) != 0 ? readFloat(&input) : 0.0f) + fromOffset[propY];
                transformData.offsetScaleX = ((flags & 8) != 0 ? readFloat(&input) : 0.0f) + fromOffset[propScaleX];
                transformData.offsetScaleY = ((flags & 16) != 0 ? readFloat(&input) : 0.0f) + fromOffset[propScaleY];
                transformData.offsetShearY = ((flags & 32) != 0 ? readFloat(&input) : 0.0f) + fromOffset[propShearY];
                flags = readByte(&input);
                float mixRaw[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
                if ((flags & 1) != 0) mixRaw[propRotate] = readFloat(&input);
                if ((flags & 2) != 0) mixRaw[propX] = readFloat(&input);
                if ((flags & 4) != 0) mixRaw[propY] = readFloat(&input);
                if ((flags & 8) != 0) mixRaw[propScaleX] = readFloat(&input);
                if ((flags & 16) != 0) mixRaw[propScaleY] = readFloat(&input);
                if ((flags & 32) != 0) mixRaw[propShearY] = readFloat(&input);
                transformData.mixRotate = enabled[propRotate] ? mixRaw[propRotate] * gain[propRotate] : 0.0f;
                transformData.mixX = enabled[propX] ? mixRaw[propX] * gain[propX] : 0.0f;
                transformData.mixY = enabled[propY] ? mixRaw[propY] * gain[propY] : 0.0f;
                transformData.mixScaleX = enabled[propScaleX] ? mixRaw[propScaleX] * gain[propScaleX] : 0.0f;
                transformData.mixScaleY = enabled[propScaleY] ? mixRaw[propScaleY] * gain[propScaleY] : 0.0f;
                transformData.mixShearY = enabled[propShearY] ? mixRaw[propShearY] * gain[propShearY] : 0.0f;
                skeletonData.transformConstraints.push_back(transformData);
                break;
            }
            case CONSTRAINT_PATH_43: {
                PathConstraintData pathData;
                pathData.name = name;
                pathData.order = constraintRefs.size();
                int bonesCount = readVarint(&input, true);
                for (int ii = 0; ii < bonesCount; ii++)
                    pathData.bones.push_back(skeletonData.bones[readVarint(&input, true)].name.value());
                pathData.target = skeletonData.slots[readVarint(&input, true)].name; // target slot
                int flags = readByte(&input);
                pathData.skinRequired = (flags & 1) != 0;
                pathData.positionMode = static_cast<PositionMode>((flags >> 1) & 1);
                pathData.spacingMode = static_cast<SpacingMode>((flags >> 2) & 3);
                pathData.rotateMode = static_cast<RotateMode>((flags >> 4) & 3);
                if ((flags & 128) != 0) pathData.offsetRotation = readFloat(&input);
                pathData.position = readFloat(&input);
                pathData.spacing = readFloat(&input);
                pathData.mixRotate = readFloat(&input);
                pathData.mixX = readFloat(&input);
                pathData.mixY = readFloat(&input);
                skeletonData.pathConstraints.push_back(pathData);
                break;
            }
            case CONSTRAINT_PHYSICS_43: {
                PhysicsConstraintData physicsData;
                physicsData.name = name;
                physicsData.order = constraintRefs.size();
                physicsData.bone = skeletonData.bones[readVarint(&input, true)].name;
                int flags = readByte(&input);
                physicsData.skinRequired = (flags & 1) != 0;
                if ((flags & 2) != 0) physicsData.x = readFloat(&input);
                if ((flags & 4) != 0) physicsData.y = readFloat(&input);
                if ((flags & 8) != 0) physicsData.rotate = readFloat(&input);
                if ((flags & 16) != 0) {
                    float scaleX = readFloat(&input);
                    // negative values encode the scaleY mode, which 4.2 cannot express
                    if (scaleX < -2) scaleX = -2 - scaleX;
                    else if (scaleX < 0) scaleX = -1 - scaleX;
                    physicsData.scaleX = scaleX;
                }
                if ((flags & 32) != 0) physicsData.shearX = readFloat(&input);
                physicsData.limit = (flags & 64) != 0 ? readFloat(&input) : 5000.0f;
                physicsData.fps = (float) readByte(&input);
                physicsData.inertia = readFloat(&input);
                physicsData.strength = readFloat(&input);
                physicsData.damping = readFloat(&input);
                physicsData.mass = (flags & 128) != 0 ? 1.0f / readFloat(&input) : 1.0f;
                physicsData.wind = readFloat(&input);
                physicsData.gravity = readFloat(&input);
                flags = readByte(&input);
                physicsData.inertiaGlobal = (flags & 1) != 0;
                physicsData.strengthGlobal = (flags & 2) != 0;
                physicsData.dampingGlobal = (flags & 4) != 0;
                physicsData.massGlobal = (flags & 8) != 0;
                physicsData.windGlobal = (flags & 16) != 0;
                physicsData.gravityGlobal = (flags & 32) != 0;
                physicsData.mixGlobal = (flags & 64) != 0;
                physicsData.mix = (flags & 128) != 0 ? readFloat(&input) : 1.0f;
                skeletonData.physicsConstraints.push_back(physicsData);
                break;
            }
            case CONSTRAINT_SLIDER_43: {
                // slider constraints are a 4.3 feature; consume bytes, drop data
                ref.name = "";
                int flags = readByte(&input);
                if ((flags & 8) != 0) readFloat(&input); // value (time or max)
                if ((flags & 16) != 0 && (flags & 32) != 0) readFloat(&input); // explicit mix
                if ((flags & 64) != 0) {
                    readVarint(&input, true); // bone index
                    readFloat(&input);        // property offset
                    readByte(&input);         // property type
                    readFloat(&input);        // offset
                    readFloat(&input);        // scale
                }
                break;
            }
        }
        constraintRefs.push_back(ref);
    }

    /* Skins */
    std::optional<Skin> defaultSkin = readSkin(&input, true, &skeletonData, constraintRefs);
    if (defaultSkin) skeletonData.skins.push_back(*defaultSkin);
    int skinCount = readVarint(&input, true);
    for (int i = 0; i < skinCount; i++) {
        skeletonData.skins.push_back(readSkin(&input, false, &skeletonData, constraintRefs).value());
    }

    /* Linkedmesh skin names (deferred until all skins are read) */
    for (auto& skin : skeletonData.skins) {
        for (auto& [slotName, slotMap] : skin.attachments) {
            for (auto& [attachmentName, attachment] : slotMap) {
                if (attachment.type == AttachmentType::AttachmentType_Linkedmesh) {
                    LinkedmeshAttachment& linkedMesh = std::get<LinkedmeshAttachment>(attachment.data);
                    if (linkedMesh.skinIndex >= 0 && linkedMesh.skinIndex < (int) skeletonData.skins.size()) {
                        std::string skinName = skeletonData.skins[linkedMesh.skinIndex].name;
                        if (skinName != "default") linkedMesh.skin = skinName;
                    }
                }
            }
        }
    }

    /* Events */
    int eventCount = readVarint(&input, true);
    for (int i = 0; i < eventCount; i++) {
        EventData eventData;
        eventData.name = readString(&input).value();
        eventData.intValue = readVarint(&input, false);
        eventData.floatValue = readFloat(&input);
        eventData.stringValue = readString(&input);
        eventData.audioPath = readString(&input);
        if (eventData.audioPath && eventData.audioPath->length() > 0) {
            eventData.volume = readFloat(&input);
            eventData.balance = readFloat(&input);
        }
        skeletonData.events.push_back(eventData);
    }

    /* Animations (trailing slider->animation indices are left unread) */
    int animationCount = readVarint(&input, true);
    for (int i = 0; i < animationCount; i++) {
        Animation animation = readAnimation(&input, &skeletonData, constraintRefs);
        skeletonData.animations.push_back(animation);
    }

    return skeletonData;
}

}
