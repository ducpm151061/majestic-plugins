#include <algorithm>
#include <iostream>
#include "iout.h"

IOUTracker::IOUTracker()
{

}

IOUTracker::~IOUTracker()
{

}

inline float intersectionOverUnion(TrackBBox box1, TrackBBox box2)
{
    float minx1 = box1.x;
    float maxx1 = box1.x + box1.w;
    float miny1 = box1.y;
    float maxy1 = box1.y + box1.h;

    float minx2 = box2.x;
    float maxx2 = box2.x + box2.w;
    float miny2 = box2.y;
    float maxy2 = box2.y + box2.h;

    if (minx1 > maxx2 || maxx1 < minx2 || miny1 > maxy2 || maxy1 < miny2)
        return 0.0f;
    else
    {
        float dx = std::min(maxx2, maxx1) - std::max(minx2, minx1);
        float dy = std::min(maxy2, maxy1) - std::max(miny2, miny1);
        float area1 = (maxx1 - minx1)*(maxy1 - miny1);
        float area2 = (maxx2 - minx2)*(maxy2 - miny2);
        float inter = dx * dy; // Intersection
        float uni = area1 + area2 - inter; // Union
        float IoU = inter / uni;
        return IoU;
    }
    //    return 0.0f;
}
 
inline int highestIOU(TrackBBox box, std::vector<TrackBBox> boxes)
{
    float iou = 0, highest = 0;
    int index = -1;
    for (size_t i = 0; i < boxes.size(); i++)
    {
        iou = intersectionOverUnion(box, boxes[i]);
        if (iou >= highest)
        {
            highest = iou;
            index = i;
        }
    }
    return index;
}

std::vector< Track > IOUTracker::track_iou(std::vector< std::vector<TrackBBox> > detections)
{
    //std::vector<Track> active_tracks;
    std::vector<Track> finished_tracks;
    int frame = 0;
    //int track_id = 1; // Starting ID for the Tracks
    int index;        // Index of the box with the highest IOU
    bool updated;    // Whether if a track was updated or not
    int numFrames = detections.size();

    //std::cout << "Num of frames = " << numFrames << std::endl;

    for (frame=0; frame < numFrames; frame++)
    {
        std::vector<TrackBBox> frameBoxes = detections[frame];
        /// Update active tracks
        for (size_t i = 0; i < active_tracks.size(); i++)
        {
            Track track = active_tracks[i];
            updated = false;
            // Get the index of the detection with the highest IOU
            index = highestIOU(track.boxes.back(), frameBoxes);

            if (index < 0)
            {
                index = 0;
            }
            // Check is above the IOU threshold
            if (frameBoxes.size() != 0) {
                float iou_value = intersectionOverUnion(track.boxes.back(), frameBoxes[index]);
                if (iou_value > iou_sigma_show) {
                    int size = track.boxes.size() - 1;
                    frameBoxes[index] = track.boxes[size];
                }

                if (index != -1 && iou_value >= IOUTracker::iou_sigma_iou)
                {
                    track.boxes.push_back(frameBoxes[index]);
                    if (track.max_score < frameBoxes[index].score)
                        track.max_score = frameBoxes[index].score;
                    // Remove the best matching detection from the frame detections
                    frameBoxes.erase(frameBoxes.begin() + index);
                    int track_boxes_size = track.boxes.size() - 1;
                    if (track_boxes_size >= iou_t_max) {
                        track.boxes.erase(track.boxes.begin());
                    }
                    active_tracks[i] = track;
                    updated = true;
                }
            }
            // If the track was not updated...
            if (!updated)
            {
                // Check the conditions to finish the track
                if (track.max_score >= iou_sigma_h && track.boxes.size() >= iou_t_min)
                    finished_tracks.push_back(track);

                active_tracks.erase(active_tracks.begin() + i);
                // Workaround used because of the previous line "erase" call
                i--;
            }

        } // End for active tracks

        /// Create new tracks
        for (auto box : frameBoxes)
        {
            std::vector<TrackBBox> b;
            b.push_back(box);
            // Track_id is set to 0 because we dont know if this track will "survive" or not
            //Track t = { b, box.score, frame, 0 };
            Track t = { b, box.score, frame, IOUTracker::track_id };
            IOUTracker::track_id++;
            active_tracks.push_back(t);

        }
    } // End of frames
    //std::cout << "Num of active_tracks = " << active_tracks.size() << std::endl;
    /// Finish the remaining tracks
    for (auto track : active_tracks)
        if (track.max_score >= iou_sigma_h && track.boxes.size() >= iou_t_min) {
            finished_tracks.push_back(track);
        }
    //std::cout << "Num of finished tracks = " << finished_tracks.size() << std::endl;

    /// Enumerate only the remaining tracks aka the ones finished
    //for (int i = 0; i < finished_tracks.size(); i++)
    //{
    //    finished_tracks[i].id = track_id;
    //    track_id++;
    //}

    return finished_tracks;
}
