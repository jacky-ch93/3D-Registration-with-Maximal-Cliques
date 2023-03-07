#include <pcl/io/pcd_io.h>
#include <pcl/io/ply_io.h>
#include <pcl/point_types.h>
#include <pcl/keypoints/harris_3d.h>
#include <pcl/features/normal_3d.h>
#include <pcl/features/normal_3d_omp.h>
#include <pcl/features/shot.h>
#include <pcl/registration/correspondence_estimation.h>
#include <pcl/registration/transformation_estimation_svd.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <direct.h>
#include <iostream>
#include <string>
#include <algorithm>
#include <io.h>
#include "omp.h"
#include "Eva.h"
#include <stdarg.h>
#include <chrono>
#include <windows.h>
#include <io.h>
using namespace Eigen;
using namespace std;
// igraph 0.9.9
extern bool add_overlap;
extern bool low_inlieratio;

bool registration(string name, string src_pointcloud, string des_pointcloud, string corr_path, string label_path, string ov_label, string gt_mat, string folderPath, double& RE, double& TE, double& inlier_num, double& total_num, double& inlier_ratio, double& success_num, double& total_estimate, string descriptor, vector<double>& time_consumption) {
	bool sc2 = true;
	bool Corr_select = false;
	bool GT_cmp_mode = false;
	int max_est_num = INT_MAX;
	bool ransc_original = false;
	string metric = "MAE";

	success_num = 0;
	if (_access(folderPath.c_str(), 0))
	{
		if (_mkdir(folderPath.c_str()) != 0) {
			cout << " 创建数据项目录失败 " << endl;
			exit(-1);
		}
	}
	cout << folderPath << endl;
	// 获取数据文件目录
	string dataPath = corr_path.substr(0, corr_path.rfind("\\"));
	// 获取当前项目名称
	string item_name = folderPath.substr(folderPath.rfind("\\") + 1, folderPath.length());

	FILE* corr, * gt;
	corr = fopen(corr_path.c_str(), "r");
	gt = fopen(label_path.c_str(), "r");
	if (corr == NULL) {
		std::cout << " error in loading correspondence data. " << std::endl;
		exit(-1);
	}
	if (gt == NULL) {
		std::cout << " error in loading ground truth label data. " << std::endl;
		exit(-1);
	}

	FILE* ov;
	vector<double>ov_corr_label;
	if (add_overlap)
	{
		ov = fopen(ov_label.c_str(), "r");
		if (ov == NULL) {
			std::cout << " error in loading overlap data. " << std::endl;
			exit(-1);
		}
		while (!feof(ov))
		{
			double value;
			fscanf(ov, "%lf\n", &value);
			ov_corr_label.push_back(value);
		}
		fclose(ov);
		cout << "load overlap data finished." << endl;
	}

	PointCloudPtr Overlap_src(new pcl::PointCloud<pcl::PointXYZ>);
	PointCloudPtr Raw_src(new pcl::PointCloud<pcl::PointXYZ>);
	PointCloudPtr Raw_des(new pcl::PointCloud<pcl::PointXYZ>);
	float raw_des_resolution = 0;
	float raw_src_resolution = 0;
	pcl::KdTreeFLANN<pcl::PointXYZ>kdtree_Overlap_des, kdtree_Overlap_src;

	PointCloudPtr cloud_src(new pcl::PointCloud<pcl::PointXYZ>);
	PointCloudPtr cloud_des(new pcl::PointCloud<pcl::PointXYZ>);
	pcl::PointCloud<pcl::Normal>::Ptr normal_src(new pcl::PointCloud<pcl::Normal>);//法向量计算结果
	pcl::PointCloud<pcl::Normal>::Ptr normal_des(new pcl::PointCloud<pcl::Normal>);
	vector<Corre_3DMatch>correspondence;
	vector<int>true_corre;
	inlier_num = 0;
	float resolution = 0;
	bool kitti = false;

	if (low_inlieratio)
	{
		if (pcl::io::loadPCDFile(src_pointcloud.c_str(), *cloud_src) < 0) {
			std::cout << " error in loading source pointcloud. " << std::endl;
			exit(-1);
		}

		if (pcl::io::loadPCDFile(des_pointcloud.c_str(), *cloud_des) < 0) {
			std::cout << " error in loading target pointcloud. " << std::endl;
			exit(-1);
		}
		while (!feof(corr)) {
			Corre_3DMatch t;
			pcl::PointXYZ src, des;
			int src_ind, des_ind;
			//fscanf(corr, "%d %d\n", &src_ind, &des_ind);
			//t.src = cloud_src->points[src_ind];
			//t.des = cloud_des->points[des_ind];
			fscanf(corr, "%f %f %f %f %f %f\n", &src.x, &src.y, &src.z, &des.x, &des.y, &des.z);
			t.src = src;
			t.des = des;
			correspondence.push_back(t);
		}
		fclose(corr);
	}
	else {
		if (name == "KITTI")//KITTI
		{
			kitti = true;
			while (!feof(corr))
			{
				Corre_3DMatch t;
				pcl::PointXYZ src, des;
				fscanf(corr, "%f %f %f %f %f %f\n", &src.x, &src.y, &src.z, &des.x, &des.y, &des.z);
				t.src = src;
				t.des = des;
				correspondence.push_back(t);
			}
			fclose(corr);
		}
		else if (name == "U3M") {
			XYZorPly_Read(src_pointcloud.c_str(), cloud_src);
			XYZorPly_Read(des_pointcloud.c_str(), cloud_des);
			float resolution_src = MeshResolution_mr_compute(cloud_src);
			float resolution_des = MeshResolution_mr_compute(cloud_des);
			resolution = (resolution_des + resolution_src) / 2;
			while (!feof(corr))
			{
				Corre_3DMatch t;
				pcl::PointXYZ src, des;
				fscanf(corr, "%f %f %f %f %f %f\n", &src.x, &src.y, &src.z, &des.x, &des.y, &des.z);
				t.src = src;
				t.des = des;
				correspondence.push_back(t);
			}
			fclose(corr);
		}
		else if (name == "3dmatch" || name == "3dlomatch") {

			if (!(src_pointcloud == "NULL" && des_pointcloud == "NULL"))
			{
				if (pcl::io::loadPLYFile(src_pointcloud.c_str(), *cloud_src) < 0) {
					std::cout << " error in loading source pointcloud. " << std::endl;
					exit(-1);
				}

				if (pcl::io::loadPLYFile(des_pointcloud.c_str(), *cloud_des) < 0) {
					std::cout << " error in loading target pointcloud. " << std::endl;
					exit(-1);
				}
				float resolution_src = MeshResolution_mr_compute(cloud_src);
				float resolution_des = MeshResolution_mr_compute(cloud_des);
				resolution = (resolution_des + resolution_src) / 2;

				pcl::NormalEstimation<pcl::PointXYZ, pcl::Normal> n_src, n_des;//法向量估计对象
				n_src.setInputCloud(cloud_src);
				n_des.setInputCloud(cloud_des);
				pcl::search::KdTree<pcl::PointXYZ>::Ptr tree_src(new pcl::search::KdTree<pcl::PointXYZ>);//创建空的kdtree
				pcl::search::KdTree<pcl::PointXYZ>::Ptr tree_des(new pcl::search::KdTree<pcl::PointXYZ>);
				n_src.setSearchMethod(tree_src);
				n_src.setKSearch(20);
				n_src.compute(*normal_src);
				n_des.setSearchMethod(tree_des);
				n_des.setKSearch(20);
				n_des.compute(*normal_des);
				pcl::KdTreeFLANN<pcl::PointXYZ> kdtree_src, kdtree_des;
				kdtree_src.setInputCloud(cloud_src);
				kdtree_des.setInputCloud(cloud_des);
				vector<int>src_ind(1), des_ind(1);
				vector<float>src_dis(1), des_dis(1);
				while (!feof(corr))
				{
					Corre_3DMatch t;
					pcl::PointXYZ src, des;
					fscanf(corr, "%f %f %f %f %f %f\n", &src.x, &src.y, &src.z, &des.x, &des.y, &des.z);
					// 寻找法向量
					kdtree_src.nearestKSearch(src, 1, src_ind, src_dis);
					kdtree_des.nearestKSearch(des, 1, des_ind, des_dis);
					Eigen::Vector3f src_vector(normal_src->points[src_ind[0]].data_n[0], normal_src->points[src_ind[0]].data_n[1], normal_src->points[src_ind[0]].data_n[2]);
					Eigen::Vector3f des_vector(normal_des->points[des_ind[0]].data_n[0], normal_des->points[des_ind[0]].data_n[1], normal_des->points[des_ind[0]].data_n[2]);
					t.src = src;
					t.des = des;
					t.src_index = src_ind[0];
					t.des_index = des_ind[0];
					t.src_norm = src_vector;
					t.des_norm = des_vector;
					t.inlier_weight = 0;
					correspondence.push_back(t);
				}
				fclose(corr);
				src_ind.clear(); des_ind.clear();
				src_dis.clear(); des_dis.clear();
			}
			else {
				int idx = 0;
				while (!feof(corr))
				{
					Corre_3DMatch t;
					pcl::PointXYZ src, des;
					fscanf(corr, "%f %f %f %f %f %f\n", &src.x, &src.y, &src.z, &des.x, &des.y, &des.z);
					t.src = src;
					t.des = des;
					t.inlier_weight = 0;
					if (add_overlap)
					{
						t.score = ov_corr_label[idx];
					}
					else
					{
						t.score = 0;
					}
					correspondence.push_back(t);
					idx++;
				}
				fclose(corr);
				if (add_overlap)
				{
					string ov_src_file = dataPath + "\\" + item_name + "_ov_src.pcd";
					string raw_src_file = dataPath + "\\" + item_name + "_raw_src.pcd";
					string raw_des_file = dataPath + "\\" + item_name + "_raw_des.pcd";
					if (pcl::io::loadPCDFile(ov_src_file.c_str(), *Overlap_src) < 0)
					{
						cout << "error in loading ov_src.pcd" << endl;
						exit(-1);
					}
					if (pcl::io::loadPCDFile(raw_des_file.c_str(), *Raw_des) < 0)
					{
						cout << "error in loading raw_des.pcd" << endl;
						exit(-1);
					}
					if (pcl::io::loadPCDFile(raw_src_file.c_str(), *Raw_src) < 0)
					{
						cout << "error in loading raw_src.pcd" << endl;
						exit(-1);
					}
					kdtree_Overlap_src.setInputCloud(Raw_src);
					kdtree_Overlap_des.setInputCloud(Raw_des);
					raw_des_resolution = MeshResolution_mr_compute(Raw_des);
					raw_src_resolution = MeshResolution_mr_compute(Raw_src);
					resolution = (raw_src_resolution + raw_des_resolution) / 2;
#pragma omp parallel for
					for (int i = 0; i < correspondence.size(); i++)
					{
						Corre_3DMatch* t = &correspondence[i];
						PointCloudPtr src_knn(new(pcl::PointCloud<pcl::PointXYZ>));
						PointCloudPtr des_knn(new(pcl::PointCloud<pcl::PointXYZ>));
						vector<int>src_ind(21), des_ind(21);
						vector<float>src_dis(21), des_dis(21);
						kdtree_Overlap_src.nearestKSearch(t->src, 21, src_ind, src_dis);
						kdtree_Overlap_des.nearestKSearch(t->des, 21, des_ind, des_dis);
						pcl::copyPointCloud(*Raw_src, src_ind, *src_knn);
						pcl::copyPointCloud(*Raw_des, des_ind, *des_knn);
						computeCentroidAndCovariance(*t, src_knn, des_knn);
					}
					cout << "number of query points: " << Overlap_src->points.size() << endl;
				}
			}
		}
		else {
			exit(-1);
		}
	}
	
	total_num = correspondence.size();
	while (!feof(gt))
	{
		int value;
		fscanf(gt, "%d\n", &value);
		true_corre.push_back(value);
		if (value == 1)
		{
			inlier_num++;
		}
	}
	fclose(gt);

	Eigen::Matrix4d GTmat;
	GTMatRead(gt_mat.c_str(), GTmat);
	inlier_ratio = 0;
	if (inlier_num == 0)
	{
		cout << " NO INLIERS！ " << endl;
	}
	inlier_ratio = inlier_num / (total_num / 1.0);
	std::chrono::time_point<std::chrono::system_clock> start, end;
	std::chrono::duration<double> elapsed_time, total_time;

	if (ransc_original)
	{
		Eigen::Matrix4f Mat;
		float RANSAC_inlier_judge_thresh = 0.1;
		float score = 0;
		bool found = false;
		pcl::PointCloud<pcl::PointXYZ>::Ptr source_match_points(new pcl::PointCloud<pcl::PointXYZ>);
		pcl::PointCloud<pcl::PointXYZ>::Ptr target_match_points(new pcl::PointCloud<pcl::PointXYZ>);
		for (int i = 0; i < correspondence.size(); i++)
		{
			pcl::PointXYZ point_s, point_t;
			point_s = correspondence[i].src;
			point_t = correspondence[i].des;
			source_match_points->points.push_back(point_s);
			target_match_points->points.push_back(point_t);
		}
		//
		total_estimate = max_est_num;
		int Iterations = max_est_num;
		int Match_Idx1, Match_Idx2, Match_Idx3;
		double re, te;

#pragma omp parallel for
		for (int Rand_seed = Iterations; Rand_seed > 0; Rand_seed--)
		{
			Rand_3(Rand_seed, correspondence.size(), Match_Idx1, Match_Idx2, Match_Idx3);
			pcl::PointXYZ point_s1, point_s2, point_s3, point_t1, point_t2, point_t3;
			point_s1 = correspondence[Match_Idx1].src;
			point_s2 = correspondence[Match_Idx2].src;
			point_s3 = correspondence[Match_Idx3].src;
			point_t1 = correspondence[Match_Idx1].des;
			point_t2 = correspondence[Match_Idx2].des;
			point_t3 = correspondence[Match_Idx3].des;
			//
			Eigen::Matrix4f Mat_iter;
			RANSAC_trans_est(point_s1, point_s2, point_s3, point_t1, point_t2, point_t3, Mat_iter);
			float score_iter = Score_est(source_match_points, target_match_points, Mat_iter, RANSAC_inlier_judge_thresh, "inlier");
			//Eigen::MatrixXd Mat_1 = Mat_iter.cast<double>();
			//bool success = evaluation_est(Mat_1, GTmat, 15, 30, re, te);
//#pragma omp critical
//			{
//				success_num = success ? success_num + 1 : success_num;
//				//找到最佳
//				if (success && re < RE && te < TE)
//				{
//					RE = re;
//					TE = te;
//					Mat = Mat_iter;
//					score = score_iter;
//					found = true;
//				}
//			}

#pragma omp critical
			{
				if (score < score_iter)
				{
					score = score_iter;
					Mat = Mat_iter;
				}
			}
		}
		//cout << success_num << " : " << max_est_num << endl;

		Eigen::MatrixXd Mat_1 = Mat.cast<double>();
		found = evaluation_est(Mat_1, GTmat, 15, 30, RE, TE);
		for (size_t i = 0; i < 4; i++)
		{
			time_consumption.push_back(0);
		}

		//保存匹配到txt
		//savetxt(correspondence, folderPath + "\\corr.txt");
		//savetxt(selected, folderPath + "\\selected.txt");
		string save_est = folderPath + "\\est.txt";
		string save_gt = folderPath + "\\GTmat.txt";
		ofstream outfile(save_est, ios::trunc);
		outfile.setf(ios::fixed, ios::floatfield);
		outfile << setprecision(10) << Mat_1;
		outfile.close();
		CopyFile(gt_mat.c_str(), save_gt.c_str(), false);
		//string save_label = folderPath + "\\label.txt";
		//CopyFile(label_path.c_str(), save_label.c_str(), false);

		//保存ply
		string save_src_cloud = folderPath + "\\source.ply";
		string save_tgt_cloud = folderPath + "\\target.ply";
		CopyFile(src_pointcloud.c_str(), save_src_cloud.c_str(), false);
		CopyFile(des_pointcloud.c_str(), save_tgt_cloud.c_str(), false);
		cout << "RE=" << RE << " " << "TE=" << TE << endl;
		if (found)
		{
			cout << Mat_1 << endl;
			return true;
		}
		return false;
	}

	//8.27 构图：一阶兼容性图能够表示节点之间的连接关系，二阶兼容性图能够去除所有没能够形成三元环的边(获得权重的重要信息！！！)
	start = std::chrono::system_clock::now();
	Eigen::MatrixXf Graph = Graph_construction(correspondence, resolution, sc2, name, descriptor);
	end = std::chrono::system_clock::now();
	elapsed_time = end - start;
	time_consumption.push_back(elapsed_time.count());
	total_time += elapsed_time;
	cout << " graph construction: " << elapsed_time.count() << endl; //需要检验图是否连通？
	if (Graph.norm() == 0) {
		return false;
	}
	/*MatD sorted_Graph;
	MatrixXi sort_index;
	sort_row(Graph, sorted_Graph, sort_index);*/

	vector<int>degree(total_num, 0);
	vector<Vote_exp> pts_degree;
	for (int i = 0; i < total_num; i++)
	{
		Vote_exp t;
		t.true_num = 0;
		vector<int> corre_index;
		for (int j = 0; j < total_num; j++)
		{
			if (i != j && Graph(i, j)) {
				degree[i] ++;
				corre_index.push_back(j);
				if (true_corre[j])
				{
					t.true_num++;
				}
			}
		}
		t.index = i;
		t.degree = degree[i];
		t.corre_index = corre_index;
		pts_degree.push_back(t);
	}

	//计算节点聚类系数
	start = std::chrono::system_clock::now();
	vector<Vote> cluster_factor;
	double sum_fenzi = 0;
	double sum_fenmu = 0;
	omp_set_num_threads(12);
	for (int i = 0; i < total_num; i++)
	{
		Vote t;
		double sum_i = 0;
		double wijk = 0;
		int index_size = pts_degree[i].corre_index.size();
#pragma omp parallel
		{
#pragma omp for
			for (int j = 0; j < index_size; j++)
			{
				int a = pts_degree[i].corre_index[j];
				for (int k = j + 1; k < index_size; k++)
				{
					int b = pts_degree[i].corre_index[k];
					if (Graph(a, b)) {
#pragma omp critical
						wijk += pow(Graph(i, a) * Graph(i, b) * Graph(a, b), 1.0 / 3); //wij + wik
					}
				}
			}
		}

		if (degree[i] > 1)
		{
			double f1 = wijk;
			double f2 = degree[i] * (degree[i] - 1) * 0.5;
			sum_fenzi += f1;
			sum_fenmu += f2;
			double factor = f1 / f2;
			t.index = i;
			t.score = factor;
			cluster_factor.push_back(t);
		}
		else {
			t.index = i;
			t.score = 0;
			cluster_factor.push_back(t);
		}
	}
	end = std::chrono::system_clock::now();
	elapsed_time = end - start;
	cout << " coefficient computation: " << elapsed_time.count() << endl;
	double average_factor = 0;
	for (size_t i = 0; i < cluster_factor.size(); i++)
	{
		average_factor += cluster_factor[i].score;
	}
	average_factor /= cluster_factor.size();

	double total_factor = sum_fenzi / sum_fenmu;

	vector<Vote_exp> pts_degree_bac;
	vector<Vote>cluster_factor_bac;
	pts_degree_bac.assign(pts_degree.begin(), pts_degree.end());
	cluster_factor_bac.assign(cluster_factor.begin(), cluster_factor.end());

	sort(cluster_factor.begin(), cluster_factor.end(), compare_vote_score);
	sort(pts_degree.begin(), pts_degree.end(), compare_vote_degree);
	string point_degree = folderPath + "\\degree.txt";
	string cluster = folderPath + "\\cluster.txt";
	FILE* exp = fopen(point_degree.c_str(), "w");
	for (size_t i = 0; i < total_num; i++)
	{
		fprintf(exp, "%d : %d ", pts_degree[i].index, pts_degree[i].degree);
		if (true_corre[pts_degree[i].index])
		{
			fprintf(exp, "1 ");
		}
		else {
			fprintf(exp, "0 ");
		}
		fprintf(exp, "%d\n", pts_degree[i].true_num);
	}
	fclose(exp);
	exp = fopen(cluster.c_str(), "w");
	for (size_t i = 0; i < total_num; i++)
	{
		fprintf(exp, "%d : %f ", cluster_factor[i].index, cluster_factor[i].score);
		if (true_corre[cluster_factor[i].index])
		{
			fprintf(exp, "1 ");
		}
		else {
			fprintf(exp, "0 ");
		}
		fprintf(exp, "%d\n", pts_degree_bac[cluster_factor[i].index].true_num);
	}
	fclose(exp);

	Eigen::VectorXd cluster_coefficients;
	cluster_coefficients.resize(cluster_factor.size());
	for (size_t i = 0; i < cluster_factor.size(); i++)
	{
		cluster_coefficients[i] = cluster_factor[i].score;
	}

	int cnt = 0;
	double OTSU = 0;
	if (cluster_factor[0].score != 0)
	{
		OTSU = OTSU_thresh(cluster_coefficients); //得到的阈值超出系数的范围？
	}
	double cluster_threshold = min(OTSU, min(average_factor, total_factor)); //问题一：这里的阈值选择能否根据图中点边关系得出？

	cout << cluster_threshold << "->min(" << average_factor << " " << total_factor << " " << OTSU << ")" << endl;
	cout << " inliers: " << inlier_num << "\ttotal num: " << total_num << "\tinlier ratio: " << inlier_ratio << endl;
	//OTSU计算权重的阈值
	double weight_thresh = cluster_threshold; //OTSU_thresh(sorted);
	if (add_overlap)
	{
		weight_thresh = 0.5;
	}
	else {
		weight_thresh = 0;
	}

	//匹配置信度评分
	if (!add_overlap)
	{
		for (size_t i = 0; i < total_num; i++)
		{
			correspondence[i].score = cluster_factor_bac[i].score;
		}
	}


	if (0 /*cluster_threshold > 2*/)
	{
		/*for (size_t i = 0; i < total_num; i++)
		{
			correspondence[i].score = pts_degree_bac[i].degree;
		}*/
		Eigen::VectorXf pt_score;
		mutual_voting(correspondence, pts_degree, cluster_factor_bac, cluster_threshold, Graph);

		sort(correspondence.begin(), correspondence.end(), compare_corres_score);
		int k = 100;
		//配准部分
		Eigen::Matrix4f Mat;
		double re, te;
		int corrected = 0;
		int iter = RANSAC_Iter_Num;

		PointCloudPtr LRF_source(new pcl::PointCloud<pcl::PointXYZ>);
		PointCloudPtr LRF_target(new pcl::PointCloud<pcl::PointXYZ>);
		vector<Corre_3DMatch>selected;
		Eigen::VectorXd weights;
		weights.resize(k);
		for (size_t j = 0; j < k; j++)
		{
			LRF_source->points.push_back(correspondence[j].src);
			LRF_target->points.push_back(correspondence[j].des);
			weights[j] = correspondence[j].score;
			selected.push_back(correspondence[j]);
		}

		RANSAC_score(selected, 1, iter, 0.1, Mat, "MAE"); //zhiqiang
		Eigen::MatrixXd Mat_1 = Mat.cast<double>();
		bool successs = evaluation_est(Mat_1, GTmat, 15, 30, re, te);
		RE = re;
		TE = te;
		if (successs)
		{
			cout << "RE=" << RE << " " << "TE=" << TE << endl;
			cout << Mat_1 << endl;
			return true;
		}
		return false;
	}
	else {
		//GTM 筛选
		vector<int>Match_inlier;
		if (Corr_select)
		{
			//GTM_corre_select(100, resolution, cloud_src, cloud_des, correspondence, Match_inlier);
			Geometric_consistency(pts_degree, Match_inlier);
		}
		/*****************************************igraph**************************************************/
		igraph_t g;
		igraph_matrix_t g_mat;
		igraph_vector_t weights;
		igraph_vector_init(&weights, Graph.rows() * (Graph.cols() - 1) / 2);
		igraph_matrix_init(&g_mat, Graph.rows(), Graph.cols());

		if (Corr_select)
		{
			if (cluster_threshold > 3) {
				double f = 10;
				while (1)
				{
					if (f * max(OTSU, total_factor) > cluster_factor[49].score)
					{
						f -= 0.05;
					}
					else {
						break;
					}
				}
				for (int i = 0; i < Graph.rows(); i++)
				{
					if (Match_inlier[i] && cluster_factor_bac[i].score > f * max(OTSU, total_factor))
					{
						for (int j = i + 1; j < Graph.cols(); j++)
						{
							if (Match_inlier[j] && cluster_factor_bac[j].score > f * max(OTSU, total_factor))
							{
								MATRIX(g_mat, i, j) = Graph(i, j);
							}
						}
					}
				}
			}
			else
			{
				for (int i = 0; i < Graph.rows(); i++)
				{
					if (Match_inlier[i])
					{
						for (int j = i + 1; j < Graph.cols(); j++)
						{
							if (Match_inlier[j])
							{
								MATRIX(g_mat, i, j) = Graph(i, j);
							}
						}
					}
				}
			}

		}
		else {
			if (cluster_threshold > 3 /*max(OTSU, total_factor) > 0.3*/) //减少图规模
			{
				double f = 10;
				while (1)
				{
					if (f * max(OTSU, total_factor) > cluster_factor[100].score)
					{
						f -= 0.05;
					}
					else {
						break;
					}
				}
				for (int i = 0; i < Graph.rows(); i++)
				{
					if (cluster_factor_bac[i].score > f * max(OTSU, total_factor))
					{
						for (int j = i + 1; j < Graph.cols(); j++)
						{
							if (cluster_factor_bac[j].score > f * max(OTSU, total_factor))
							{
								MATRIX(g_mat, i, j) = Graph(i, j);
							}
						}
					}
				}
			}
			else {
				for (int i = 0; i < Graph.rows(); i++)
				{
					for (int j = i + 1; j < Graph.cols(); j++)
					{
						if (Graph(i, j))
						{
							MATRIX(g_mat, i, j) = Graph(i, j);
						}
					}
				}
			}
		}

		igraph_set_attribute_table(&igraph_cattribute_table);
		igraph_weighted_adjacency(&g, &g_mat, IGRAPH_ADJ_UNDIRECTED, 0, 1);
		const char* att = "weight";
		EANV(&g, att, &weights);
		
		//找出所有最大团
		igraph_vector_ptr_t cliques;
		igraph_vector_ptr_init(&cliques, 0);
		start = std::chrono::system_clock::now();

		igraph_maximal_cliques(&g, &cliques, 3, 0); //3dlomatch 4 3dmatch; 3 Kitti  4
		//igraph_largest_cliques(&g, &cliques);
		end = std::chrono::system_clock::now();
		elapsed_time = end - start;
		time_consumption.push_back(elapsed_time.count());
		total_time += elapsed_time;
		//print_and_destroy_cliques(&cliques);
		int clique_num = igraph_vector_ptr_size(&cliques);
		if (clique_num == 0) {
			cout << " NO CLIQUES! " << endl;
		}
		cout << " clique computation: " << elapsed_time.count() << endl;

		//数据清理
		igraph_destroy(&g);
		igraph_matrix_destroy(&g_mat);
		igraph_vector_destroy(&weights);

		vector<int>remain;
		start = std::chrono::system_clock::now();
		for (int i = 0; i < clique_num; i++)
		{
			remain.push_back(i);
		}
		node_cliques* N_C = new node_cliques[total_num];
		find_largest_clique_of_node(Graph, &cliques, correspondence, N_C, remain, total_num, max_est_num, descriptor);
		end = std::chrono::system_clock::now();
		elapsed_time = end - start;
		time_consumption.push_back(elapsed_time.count());
		total_time += elapsed_time;
		cout << " clique selection: " << elapsed_time.count() << endl;

		PointCloudPtr src_corr_pts(new pcl::PointCloud<pcl::PointXYZ>);
		PointCloudPtr des_corr_pts(new pcl::PointCloud<pcl::PointXYZ>);
		//vector<Corre_3DMatch>correspondence_selected;
		for (size_t i = 0; i < correspondence.size(); i++)
		{
			//if (correspondence[i].score > weight_thresh) {
				src_corr_pts->push_back(correspondence[i].src);
				des_corr_pts->push_back(correspondence[i].des);
			//}
		}
		
		/******************************************配准部分***************************************************/
		double RE_thresh, TE_thresh, inlier_thresh;
		if (name == "KITTI")
		{
			RE_thresh = 5;
			TE_thresh = 60;
			inlier_thresh = 0.6;
		}
		else if (name == "3dmatch" || name == "3dlomatch")
		{
			RE_thresh = 15;
			TE_thresh = 30;
			inlier_thresh = 0.1;
		}
		else if (name == "U3M") {
			inlier_thresh = 5 * resolution;
		}
		RE = RE_thresh;
		TE = TE_thresh;
		Eigen::Matrix4d best_est;

		bool found = false;
		double best_score = 0;
		vector<Corre_3DMatch>selected;
		vector<int>corre_index;
		start = std::chrono::system_clock::now();
		total_estimate = remain.size();
#pragma omp parallel for
		for (int i = 0; i < remain.size(); i++)
		{
			vector<Corre_3DMatch>Group;
			vector<int>selected_index;
			igraph_vector_t* v = (igraph_vector_t*)VECTOR(cliques)[remain[i]];
			int group_size = igraph_vector_size(v);
			for (int j = 0; j < group_size; j++)
			{
				Corre_3DMatch C = correspondence[VECTOR(*v)[j]];
				Group.push_back(C);
				selected_index.push_back(VECTOR(*v)[j]);
			}
			igraph_vector_destroy(v);
			Eigen::Matrix4d est_trans;
			//团结构评分
			double score = evaluation_trans(Group, correspondence, src_corr_pts, des_corr_pts, weight_thresh, est_trans, inlier_thresh, metric, Overlap_src, kdtree_Overlap_des, raw_des_resolution);

			if (GT_cmp_mode)
			{
				//GT已知
				if (score > 0)
				{
					//评估est
					double re, te;
					//8.27 
					//1.启发式评估函数设计->设计能够评估团的置信度的指标->匹配之间的平行关系
					//2.匹配权值设计->聚类系数
					bool success = evaluation_est(est_trans, GTmat, 15, 30, re, te);
#pragma omp critical
					{
						success_num = success ? success_num + 1 : success_num;
						//找到最佳
						if (success && re < RE && te < TE)
						{
							RE = re;
							TE = te;
							best_est = est_trans;
							best_score = score;
							selected = Group;
							corre_index = selected_index;
							found = TRUE;
						}
					}
				}
			}
			else {
				//GT未知
				if (score > 0)
				{
#pragma omp critical
					{
						if (best_score < score)
						{
							best_score = score;
							best_est = est_trans;
							selected = Group;
							corre_index = selected_index;
						}
					}
				}
			}
			
		}
		end = std::chrono::system_clock::now();
		elapsed_time = end - start;
		time_consumption.push_back(elapsed_time.count());
		total_time += elapsed_time;
		cout << " hypothesis generation & evaluation: " << elapsed_time.count() << endl;
		//释放内存空间
		igraph_vector_ptr_destroy(&cliques);
		cout << success_num << " : " << total_estimate << " : " << clique_num << endl;
		Eigen::MatrixXd tmp_best;

		if (name == "U3M")
		{
			RE = RMSE_compute(cloud_src, cloud_des, best_est, GTmat, resolution);
			TE = 0;
		}
		else {
			if (!found)
			{
				found = evaluation_est(best_est, GTmat, RE_thresh, TE_thresh, RE, TE);
			}
			tmp_best = best_est;
			post_refinement(correspondence, src_corr_pts, des_corr_pts, best_est, best_score, inlier_thresh, 20, "MAE");
		}

		cout << selected.size() << " " << best_score << endl;

		for (int i = 0; i < selected.size(); i++)
		{
			//cout << cluster_factor_bac[corre_index[i]].score << " ";
			cout << selected[i].score << " ";
		}
		cout << endl;
		if (inlier_ratio <= 0.01)
		{
			/*PointCloudPtr keypoint_src(new pcl::PointCloud<pcl::PointXYZ>);
			PointCloudPtr keypoint_des(new pcl::PointCloud<pcl::PointXYZ>);
			for (size_t i = 0; i < selected.size(); i++)
			{
				keypoint_src->points.push_back(selected[i].src);
				keypoint_des->points.push_back(selected[i].des);
			}*/
			/*cloud_src = Raw_src;
			cloud_des = Raw_des;
			Corres_selected_visual(cloud_src, cloud_des, correspondence, resolution, 0.1, GTmat);
			Corres_selected_visual(cloud_src, cloud_des, selected, resolution, 0.1, GTmat);*/
		}

		/*vector<Vote>clique_number;
		for (int i = 0; i < total_num; i++)
		{
			Vote t;
			t.index = i;
			t.score = N_C[i].clique_num;
			clique_number.push_back(t);
		}
		sort(clique_number.begin(), clique_number.end(), compare_vote_score);
		ofstream out;
		string outfile = folderPath + "\\clique_num.txt";
		out.open(outfile.c_str(), ios::out);
		for (int i = 0; i < total_num; i++)
		{
			out << clique_number[i].index << " " << clique_number[i].score << " " << true_corre[clique_number[i].index] << endl;
		}
		out.close();*/
		//内存回收
		/*if (inlier_ratio < 0.05)
		{
			Corres_selected_visual(cloud_src, cloud_des, correspondence, resolution, 0.1, GTmat);
		}*/

		//保存匹配到txt
		savetxt(correspondence, folderPath + "\\corr.txt");
		savetxt(selected, folderPath + "\\selected.txt");
		string save_est = folderPath + "\\est.txt";
		string save_gt = folderPath + "\\GTmat.txt";
		ofstream outfile(save_est, ios::trunc);
		outfile.setf(ios::fixed, ios::floatfield);
		outfile << setprecision(10) << best_est;
		outfile.close();
		CopyFile(gt_mat.c_str(), save_gt.c_str(), false);
		string save_label = folderPath + "\\label.txt";
		CopyFile(label_path.c_str(), save_label.c_str(), false);

		//保存ply
		string save_src_cloud = folderPath + "\\source.ply";
		string save_tgt_cloud = folderPath + "\\target.ply";
		//CopyFile(src_pointcloud.c_str(), save_src_cloud.c_str(), false);
		//CopyFile(des_pointcloud.c_str(), save_tgt_cloud.c_str(), false);

		correspondence.clear();
		//correspondence.shrink_to_fit();
		//correspondence_selected.clear();
		true_corre.clear();
		true_corre.shrink_to_fit();
		degree.clear();
		degree.shrink_to_fit();
		pts_degree.clear();
		pts_degree.shrink_to_fit();
		pts_degree_bac.clear();
		pts_degree_bac.shrink_to_fit();
		cluster_factor.clear();
		cluster_factor.shrink_to_fit();
		cluster_factor_bac.clear();
		cluster_factor_bac.shrink_to_fit();
		delete[] N_C;
		remain.clear();
		remain.shrink_to_fit();
		selected.clear();
		selected.shrink_to_fit();
		corre_index.clear();
		corre_index.shrink_to_fit();
		src_corr_pts.reset(new pcl::PointCloud<pcl::PointXYZ>);
		des_corr_pts.reset(new pcl::PointCloud<pcl::PointXYZ>);
		cloud_src.reset(new pcl::PointCloud<pcl::PointXYZ>);
		cloud_des.reset(new pcl::PointCloud<pcl::PointXYZ>);
		normal_src.reset(new pcl::PointCloud<pcl::Normal>);
		normal_des.reset(new pcl::PointCloud<pcl::Normal>);
		Raw_src.reset(new pcl::PointCloud<pcl::PointXYZ>);
		Raw_des.reset(new pcl::PointCloud<pcl::PointXYZ>);
		Overlap_src.reset(new pcl::PointCloud<pcl::PointXYZ>);

		if (name == "U3M")
		{
			if (RE <= 5)
			{
				cout << RE << endl;
				cout << best_est << endl;
				return TRUE;
			}
			else {
				return FALSE;
			}
		}
		else {
			//visualization(Overlap_src, Raw_src, Raw_des, selected, GTmat, GTmat, resolution);
			if (found)
			{
				double new_re, new_te;
				evaluation_est(best_est, GTmat, RE_thresh, TE_thresh, new_re, new_te);
				if (new_re < RE && new_te < TE)
				{
					RE = new_re;
					TE = new_te;
					cout << "est_trans updated!!!" << endl;
					cout << "RE=" << RE << " " << "TE=" << TE << endl;
					cout << best_est << endl;
				}
				else {
					best_est = tmp_best;
					cout << "RE=" << RE << " " << "TE=" << TE << endl;
					cout << best_est << endl;
				}

				/*if (inlier_ratio < 0.05)
				{
					Corres_selected_visual(cloud_src, cloud_des, selected, resolution, 0.1, GTmat);
					visualization(cloud_src, cloud_des, best_est, resolution);
				}*/

				return TRUE;
			}
			else {
				double new_re, new_te;
				found = evaluation_est(best_est, GTmat, RE_thresh, TE_thresh, new_re, new_te);
				if (found)
				{
					RE = new_re;
					TE = new_te;
					cout << "est_trans corrected!!!" << endl;
					cout << "RE=" << RE << " " << "TE=" << TE << endl;
					cout << best_est << endl;
					return TRUE;
				}
				//Corres_selected_visual(Raw_src, Raw_des, correspondence, resolution, 0.1, GTmat);
				//Corres_selected_visual(Raw_src, Raw_des, selected, resolution, 0.1, GTmat);
				
				cout << "RE=" << RE << " " << "TE=" << TE << endl;
				return FALSE;
			}
		}
	}
}